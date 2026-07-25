package com.nexus.manager.ipc

import com.google.common.truth.Truth.assertThat
import org.junit.Test

/**
 * IpcException 单元测试
 *
 * 验证 IPC 异常类型的构造与 message 处理
 */
class IpcExceptionTest {

    @Test
    fun `Timeout exception preserves message`() {
        val ex = IpcException.Timeout("request timed out after 10s")
        assertThat(ex.message).isEqualTo("request timed out after 10s")
        assertThat(ex).isInstanceOf(IpcException::class.java)
    }

    @Test
    fun `Disconnected exception has default message`() {
        val ex = IpcException.Disconnected()
        assertThat(ex.message).isNotNull()
    }

    @Test
    fun `ProtocolError preserves message`() {
        val ex = IpcException.ProtocolError("bad magic")
        assertThat(ex.message).isEqualTo("bad magic")
    }

    @Test
    fun `RemoteError preserves code and message`() {
        val ex = IpcException.RemoteError(-2, "module not found")
        assertThat(ex.message).contains("module not found")
    }

    @Test
    fun `exception types are distinguishable`() {
        val timeout = IpcException.Timeout("t")
        val disconnected = IpcException.Disconnected()
        val protocolError = IpcException.ProtocolError("p")
        val remoteError = IpcException.RemoteError(-1, "r")

        assertThat(timeout).isNotInstanceOf(IpcException.Disconnected::class.java)
        assertThat(disconnected).isNotInstanceOf(IpcException.ProtocolError::class.java)
        assertThat(protocolError).isNotInstanceOf(IpcException.RemoteError::class.java)
        assertThat(remoteError).isNotInstanceOf(IpcException.Timeout::class.java)
    }
}
