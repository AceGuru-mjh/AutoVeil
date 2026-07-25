package com.nexus.manager.ipc

import com.google.common.truth.Truth.assertThat
import kotlinx.coroutines.runBlocking
import org.junit.Test

/**
 * ReconnectStrategy 单元测试
 *
 * 验证指数退避 + 抖动的行为：
 * - attempt 越大延迟越大
 * - 延迟不超过 maxDelayMs
 * - maxAttempts 到达后立即返回
 */
class ReconnectStrategyTest {

    @Test
    fun `await returns immediately when attempt exceeds maxAttempts`() = runBlocking {
        val strategy = ReconnectStrategy(maxAttempts = 5)
        val start = System.currentTimeMillis()
        strategy.await(10)   // 10 > 5，应立即返回
        val elapsed = System.currentTimeMillis() - start
        assertThat(elapsed).isLessThan(100L)
    }

    @Test
    fun `await delays for small attempt`() = runBlocking {
        val strategy = ReconnectStrategy(baseDelayMs = 50, maxDelayMs = 200, maxAttempts = 30)
        val start = System.currentTimeMillis()
        strategy.await(1)   // 第一次重连
        val elapsed = System.currentTimeMillis() - start
        // attempt=1: 50 * 2^0 = 50ms base + 抖动(0~12)
        assertThat(elapsed).isAtLeast(50L)
        assertThat(elapsed).isAtMost(200L)
    }

    @Test
    fun `await does not exceed maxDelayMs`() = runBlocking {
        val strategy = ReconnectStrategy(baseDelayMs = 500, maxDelayMs = 1000, maxAttempts = 30)
        val start = System.currentTimeMillis()
        strategy.await(20)   // 高 attempt，应该被 maxDelayMs 限制
        val elapsed = System.currentTimeMillis() - start
        // 500 * 2^19 远超 1000，应被 capped 到 1000 + 抖动 0~250
        assertThat(elapsed).isAtLeast(1000L)
        assertThat(elapsed).isAtMost(1300L)
    }

    @Test
    fun `default maxAttempts is 30`() = runBlocking {
        val strategy = ReconnectStrategy()
        // attempt=30 应该被拒绝
        val start = System.currentTimeMillis()
        strategy.await(30)
        val elapsed = System.currentTimeMillis() - start
        assertThat(elapsed).isLessThan(50L)
    }
}
