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
    private val maxAttempts: Int = Int.MAX_VALUE
) {
    suspend fun await(attempt: Int) {
        if (attempt >= maxAttempts) return
        // 指数退避：500ms, 1s, 2s, 4s, 8s, 15s, 15s...
        val exp = (baseDelayMs * 2.0.pow(attempt.toDouble())).toLong()
        val capped = min(exp, maxDelayMs)
        // 抖动：0 ~ capped/4
        val jitter = Random.nextLong(0, capped / 4 + 1)
        delay(capped + jitter)
    }
}
