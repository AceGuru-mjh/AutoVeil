package com.nexus.manager.ipc

import android.net.LocalSocket
import android.net.LocalSocketAddress
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import java.io.IOException
import java.io.InputStream
import java.io.OutputStream

/**
 * Unix Domain Socket 传输层
 * 仅负责 socket 连接与字节流读写，帧编解码在 ProtobufCodec 中做
 */
class IpcTransport(
    private val socketPath: String = DAEMON_SOCKET_PATH
) {
    enum class State { Disconnected, Connecting, Connected, Reconnecting }

    private val _state = MutableStateFlow(State.Disconnected)
    val state: StateFlow<State> = _state

    private var socket: LocalSocket? = null
    private var input: InputStream? = null
    private var output: OutputStream? = null

    @Throws(IOException::class)
    fun connect(timeoutMs: Int = 3000) {
        _state.value = State.Connecting
        try {
            val s = LocalSocket().apply {
                soTimeout = timeoutMs
                connect(LocalSocketAddress(socketPath, LocalSocketAddress.Namespace.FILESYSTEM))
            }
            socket = s
            input = s.inputStream
            output = s.outputStream
            _state.value = State.Connected
        } catch (e: IOException) {
            _state.value = State.Disconnected
            throw e
        }
    }

    fun read(buf: ByteArray, off: Int, len: Int): Int {
        return input?.read(buf, off, len) ?: -1
    }

    fun write(buf: ByteArray, off: Int, len: Int) {
        output?.write(buf, off, len)
        output?.flush()
    }

    fun close() {
        try { input?.close() } catch (_: IOException) {}
        try { output?.close() } catch (_: IOException) {}
        try { socket?.close() } catch (_: IOException) {}
        input = null; output = null; socket = null
        _state.value = State.Disconnected
    }

    fun isConnected(): Boolean = _state.value == State.Connected

    companion object {
        const val DAEMON_SOCKET_PATH = "/dev/socket/nexusd.sock"
    }
}
