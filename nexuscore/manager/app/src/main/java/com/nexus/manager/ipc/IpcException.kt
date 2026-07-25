package com.nexus.manager.ipc

/**
 * IPC 层统一异常
 */
sealed class IpcException(message: String) : Exception(message) {
    class Timeout(msg: String) : IpcException(msg)
    class Disconnected : IpcException("connection lost")
    class Unauthorized(msg: String) : IpcException(msg)
    class ProtocolError(msg: String) : IpcException(msg)
    class RemoteError(val code: Int, msg: String) : IpcException("remote error $code: $msg")
}
