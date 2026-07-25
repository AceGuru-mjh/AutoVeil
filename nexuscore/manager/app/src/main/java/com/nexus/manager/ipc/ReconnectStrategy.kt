package com.nexus.manager.ipc

import kotlinx.coroutines.delay
import kotlin.math.min
import kotlin.math.pow
import kotlin.random.Random

/**
 * 指数退避 + 抖动重连策略
 */
class ReconnectStrategy(
    private val baseDelayMs: Long = 500L,
    private val maxDelayMs: Long = 15_000L,
    // 整改 B7：原值 Int.MAX_VALUE，配合 NexusIpcClient 重连循环会永不放弃，电池耗尽。
    // 改为有限值，与 NexusIpcClient.MAX_RECONNECT_ATTEMPTS 协同。
    private val maxAttempts: Int = 30,
) {
    suspend fun await(attempt: Int) {
        if (attempt >= maxAttempts) return
        // 指数退避：500ms, 1s, 2s, 4s, 8s, 15s, 15s...
        // 注意：attempt 已在 NexusIpcClient 中先自增再传入，因此 attempt=1 表示第一次重连。
        val exp = (baseDelayMs * 2.0.pow((attempt - 1).coerceAtLeast(0).toDouble())).toLong()
        val capped = min(exp, maxDelayMs)
        // 抖动：0 ~ capped/4
        val jitter = if (capped > 0) Random.nextLong(0, capped / 4 + 1) else 0L
        delay(capped + jitter)
    }
}
