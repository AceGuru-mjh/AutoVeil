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
