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
