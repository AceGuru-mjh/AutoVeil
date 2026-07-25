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
