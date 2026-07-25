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

import com.nexus.manager.ipc.proto.Envelope
import java.io.IOException
import java.nio.ByteBuffer
import java.nio.ByteOrder

/**
 * 帧编解码
 * 帧格式：[4B little-endian length][N bytes Envelope protobuf]
 */
object ProtobufCodec {

    const val MAGIC: Int = 0x4E58434F  // 'NXCO'
    private const val MAX_FRAME = 8 * 1024 * 1024  // 8 MiB

    fun write(transport: IpcTransport, env: Envelope) {
        val payload = env.toByteArray()
        val header = ByteBuffer.allocate(4).order(ByteOrder.LITTLE_ENDIAN)
            .putInt(payload.size).array()
        transport.write(header, 0, 4)
        transport.write(payload, 0, payload.size)
    }

    @Throws(IOException::class)
    fun read(transport: IpcTransport): Envelope {
        val header = ByteArray(4)
        if (readFully(transport, header, 0, 4) != 4)
            throw IOException("EOF reading header")
        val len = ByteBuffer.wrap(header).order(ByteOrder.LITTLE_ENDIAN).int
        if (len <= 0 || len > MAX_FRAME) throw IOException("bad frame len: $len")

        val payload = ByteArray(len)
        if (readFully(transport, payload, 0, len) != len)
            throw IOException("EOF reading payload")
        val env = Envelope.parseFrom(payload)
        if (env.magic != MAGIC) throw IOException("bad magic: 0x${env.magic.toString(16)}")
        return env
    }

    private fun readFully(t: IpcTransport, b: ByteArray, off: Int, len: Int): Int {
        var read = 0
        while (read < len) {
            val n = t.read(b, off + read, len - read)
            if (n <= 0) break
            read += n
        }
        return read
    }
}
