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
            // 整改 B1（原 bug）：原写法 `soTimeout = timeoutMs` 会让后续所有 read 最多阻塞 3 秒，
            // readerLoop 期望无限阻塞等待事件/响应 → 3 秒后 SocketTimeoutException → 死循环重连。
            // 正确做法：connect 用 timeoutMs 控制连接建立超时，连接成功后立即 soTimeout = 0（无限阻塞）。
            val s = LocalSocket()
            s.connect(LocalSocketAddress(socketPath, LocalSocketAddress.Namespace.FILESYSTEM), timeoutMs)
            s.soTimeout = 0   // 关键：恢复无限阻塞，让 readerLoop 长等待
            socket = s
            input = s.inputStream
            output = s.outputStream
            _state.value = State.Connected
        } catch (e: IOException) {
            _state.value = State.Disconnected
            throw e
        }
    }

    /**
     * 读字节到 buf，返回实际读取数；EOF/未连接返回 -1。
     * 整改 B13：原实现 input 为 null 时静默返回 -1，调用方 (ProtobufCodec) 把 -1 当 EOF
     * 静默 break，导致 readerLoop 退出但无错误日志。改为显式抛 IOException 让上层记录原因。
     */
    @Throws(IOException::class)
    fun read(buf: ByteArray, off: Int, len: Int): Int {
        val ins = input ?: throw IOException("not connected (input stream is null)")
        val n = ins.read(buf, off, len)
        if (n < 0) throw IOException("EOF (read returned $n)")
        return n
    }

    @Throws(IOException::class)
    fun write(buf: ByteArray, off: Int, len: Int) {
        val outs = output ?: throw IOException("not connected (output stream is null)")
        outs.write(buf, off, len)
        outs.flush()
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
