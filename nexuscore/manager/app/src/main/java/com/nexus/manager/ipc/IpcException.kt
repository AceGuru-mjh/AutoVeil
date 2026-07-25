/*
 * AutoVeil / NexusCore
 * Copyright (C) 2026 AutoVeil / NexusCore Contributors
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
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
