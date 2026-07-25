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
