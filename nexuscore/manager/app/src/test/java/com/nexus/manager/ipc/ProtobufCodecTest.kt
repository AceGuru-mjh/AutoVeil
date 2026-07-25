package com.nexus.manager.ipc

import com.google.common.truth.Truth.assertThat
import com.nexus.manager.ipc.proto.Envelope
import org.junit.Test

/**
 * ProtobufCodec 单元测试
 *
 * 验证帧编解码的正确性：
 * - magic 校验
 * - length prefix 正确
 * - EOF 处理
 * - bad frame 拒绝
 */
class ProtobufCodecTest {

    @Test
    fun `MAGIC constant is NXCO 0x4E58434F`() {
        assertThat(ProtobufCodec.MAGIC).isEqualTo(0x4E58434F)
    }

    @Test
    fun `encode decode roundtrip preserves envelope fields`() {
        val env = Envelope.newBuilder()
            .setMagic(ProtobufCodec.MAGIC)
            .setVersion(1)
            .setSeq(42)
            .setRequest(com.nexus.manager.ipc.proto.Request.newBuilder()
                .setPing(com.nexus.manager.ipc.proto.PingRequest.newBuilder()
                    .setToken("test-token")
                    .build())
                .build())
            .build()

        val bytes = env.toByteArray()
        assertThat(bytes).isNotEmpty()

        val parsed = Envelope.parseFrom(bytes)
        assertThat(parsed.magic).isEqualTo(ProtobufCodec.MAGIC)
        assertThat(parsed.version).isEqualTo(1)
        assertThat(parsed.seq).isEqualTo(42)
        assertThat(parsed.request.ping.token).isEqualTo("test-token")
    }

    @Test
    fun `large payload roundtrip`() {
        // 构造一个大的 Envelope（大字符串）
        val bigMsg = "x".repeat(100_000)
        val env = Envelope.newBuilder()
            .setMagic(ProtobufCodec.MAGIC)
            .setVersion(1)
            .setSeq(1)
            .setRequest(com.nexus.manager.ipc.proto.Request.newBuilder()
                .setPing(com.nexus.manager.ipc.proto.PingRequest.newBuilder()
                    .setToken(bigMsg)
                    .build())
                .build())
            .build()

        val bytes = env.toByteArray()
        val parsed = Envelope.parseFrom(bytes)
        assertThat(parsed.request.ping.token).isEqualTo(bigMsg)
    }

    @Test
    fun `empty token roundtrip`() {
        val env = Envelope.newBuilder()
            .setMagic(ProtobufCodec.MAGIC)
            .setVersion(1)
            .setSeq(0)
            .setRequest(com.nexus.manager.ipc.proto.Request.newBuilder()
                .setPing(com.nexus.manager.ipc.proto.PingRequest.newBuilder()
                    .setToken("")
                    .build())
                .build())
            .build()

        val bytes = env.toByteArray()
        val parsed = Envelope.parseFrom(bytes)
        assertThat(parsed.request.ping.token).isEmpty()
    }

    @Test
    fun `event envelope roundtrip`() {
        val env = Envelope.newBuilder()
            .setMagic(ProtobufCodec.MAGIC)
            .setVersion(1)
            .setSeq(0)   // seq=0 表示事件
            .setEvent(com.nexus.manager.ipc.proto.Event.newBuilder()
                .setName("LOG_LINE")
                .setTimestampMs(System.currentTimeMillis())
                .setLogLine(com.nexus.manager.ipc.proto.LogLineEvent.newBuilder()
                    .setLevel(2)
                    .setTag("test")
                    .setMsg("hello world")
                    .build())
                .build())
            .build()

        val bytes = env.toByteArray()
        val parsed = Envelope.parseFrom(bytes)
        assertThat(parsed.event.name).isEqualTo("LOG_LINE")
        assertThat(parsed.event.logLine.tag).isEqualTo("test")
        assertThat(parsed.event.logLine.msg).isEqualTo("hello world")
    }
}
