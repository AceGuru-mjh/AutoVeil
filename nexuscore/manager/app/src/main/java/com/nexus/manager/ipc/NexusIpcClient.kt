/*
 * AutoVeil / NexusCore
 * Copyright (c) 2026 AutoVeil / NexusCore Contributors
 *
 * Licensed under the MIT License with Non-Commercial Clause (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License in the LICENSE file at the project root.
 *
 * Commercial use requires a separate written license from the copyright holders.
 */

package com.nexus.manager.ipc

import com.nexus.manager.ipc.proto.*
import kotlinx.coroutines.*
import kotlinx.coroutines.channels.Channel
import kotlinx.coroutines.flow.*
import kotlinx.coroutines.sync.Mutex
import kotlinx.coroutines.sync.withLock
import java.io.IOException
import java.util.concurrent.atomic.AtomicInteger
import kotlinx.coroutines.currentCoroutineContext

/**
 * NexusCore IPC 客户端
 *
 * - UDS 传输 + Protobuf 编解码
 * - 自动断线重连（指数退避 + 抖动）
 * - 请求-响应按 seq 配对
 * - 事件用 SharedFlow 多订阅共享
 * - 所有 pending 请求在断线时立即失败
 */
class NexusIpcClient(
    private val transport: IpcTransport = IpcTransport(),
    private val reconnect: ReconnectStrategy = ReconnectStrategy(),
    private val io: CoroutineDispatcher = Dispatchers.IO
) {
    sealed class Connection {
        object Connected : Connection()
        data class Reconnecting(val attempt: Int) : Connection()
        data class Failed(val cause: Throwable) : Connection()
    }

    private val _connection = MutableStateFlow<Connection>(Connection.Reconnecting(0))
    val connection: StateFlow<Connection> = _connection

    private val _events = MutableSharedFlow<Event>(extraBufferCapacity = 128)
    val events: SharedFlow<Event> = _events.asSharedFlow()

    private val seqCounter = AtomicInteger(1)
    private val pending = mutableMapOf<Int, CompletableDeferred<Response>>()
    private val pendingMu = Mutex()

    private val sendQueue = Channel<Envelope>(capacity = Channel.UNLIMITED)
    private var supervisor: Job? = null

    fun start(scope: CoroutineScope) {
        supervisor?.cancel()
        supervisor = scope.launch(io) { connectionLoop() }
    }

    fun stop() {
        supervisor?.cancel()
        transport.close()
    }

    /** 同步式请求-响应，超时 10s */
    suspend fun request(req: Request): Response {
        val seq = seqCounter.getAndIncrement()
        val env = Envelope.newBuilder()
            .setMagic(ProtobufCodec.MAGIC)
            .setVersion(1)
            .setSeq(seq)
            .setRequest(req)
            .build()
        val deferred = CompletableDeferred<Response>()
        pendingMu.withLock { pending[seq] = deferred }
        return try {
            sendQueue.send(env)
            withTimeoutOrNull(REQUEST_TIMEOUT_MS) { deferred.await() }
                ?: throw IpcException.Timeout("request seq=$seq timed out")
        } catch (e: Exception) {
            pendingMu.withLock { pending.remove(seq) }
            throw if (e is IpcException) e else IpcException.ProtocolError(e.message ?: "unknown")
        }
    }

    private suspend fun connectionLoop() {
        var attempt = 0
        while (currentCoroutineContext().isActive) {
            try {
                _connection.value = Connection.Reconnecting(attempt)
                transport.connect()
                _connection.value = Connection.Connected
                attempt = 0

                coroutineScope {
                    val reader = launch(io) { readerLoop() }
                    val writer = launch(io) { writerLoop() }
                    reader.join()
                    writer.cancel()
                }
            } catch (e: CancellationException) {
                throw e
            } catch (e: Throwable) {
                _connection.value = Connection.Reconnecting(attempt)
                attempt++
            } finally {
                transport.close()
                // 断线时所有 pending 立即失败
                pendingMu.withLock {
                    pending.values.forEach { it.completeExceptionally(IpcException.Disconnected()) }
                    pending.clear()
                }
            }
            if (currentCoroutineContext().isActive) reconnect.await(attempt)
        }
    }

    private suspend fun readerLoop() {
        while (currentCoroutineContext().isActive) {
            val env = try {
                ProtobufCodec.read(transport)
            } catch (e: IOException) {
                break
            }
            when (env.bodyCase) {
                Envelope.BodyCase.RESPONSE -> {
                    val resp = env.response
                    // seq 在 Envelope 上，非 Response
                    pendingMu.withLock { pending.remove(env.seq)?.complete(resp) }
                }
                Envelope.BodyCase.EVENT -> {
                    _events.emit(env.event)
                }
                else -> { /* 忽略 */ }
            }
        }
    }

    private suspend fun writerLoop() {
        for (env in sendQueue) {
            try {
                ProtobufCodec.write(transport, env)
            } catch (e: IOException) {
                break
            }
        }
    }

    companion object {
        private const val REQUEST_TIMEOUT_MS = 10_000L
    }
}
