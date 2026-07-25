package com.nexus.manager.data.model

import com.google.common.truth.Truth.assertThat
import org.junit.Test

/**
 * UI 数据模型单元测试
 *
 * 覆盖：
 * - SystemStatus.EMPTY 默认值
 * - ModuleUi 数据类
 * - SuAppUi / SuLogEntryUi 数据类
 * - LogLine.levelName 转换
 * - SuPolicy 枚举
 */
class UiModelsTest {

    @Test
    fun `SystemStatus EMPTY has expected defaults`() {
        val s = SystemStatus.EMPTY
        assertThat(s.rootAvailable).isFalse()
        assertThat(s.rootProvider).isEqualTo("—")
        assertThat(s.daemonRunning).isFalse()
        assertThat(s.moduleCount).isEqualTo(0)
        assertThat(s.safeMode).isFalse()
        assertThat(s.uptimeMs).isEqualTo(0L)
    }

    @Test
    fun `SystemStatus copy works correctly`() {
        val s = SystemStatus.EMPTY.copy(daemonPid = 1234, moduleCount = 5)
        assertThat(s.daemonPid).isEqualTo(1234)
        assertThat(s.moduleCount).isEqualTo(5)
        // 其他字段保持默认
        assertThat(s.rootAvailable).isFalse()
    }

    @Test
    fun `ModuleUi defaults are correct`() {
        val m = ModuleUi(
            id = "test_mod",
            name = "Test Module",
            version = "1.0.0",
            author = "tester",
            description = "a test",
            enabled = true,
            priority = 100,
            capabilities = listOf("EXECUTE_SHELL", "MOUNT_FILESYSTEM"),
        )
        assertThat(m.hasUpdate).isFalse()   // 默认
        assertThat(m.updateUrl).isEmpty()
        assertThat(m.capabilities).hasSize(2)
    }

    @Test
    fun `LogLine levelName maps levels correctly`() {
        assertThat(LogLine(0, "t", "m", 0).levelName).isEqualTo("V")
        assertThat(LogLine(1, "t", "m", 0).levelName).isEqualTo("D")
        assertThat(LogLine(2, "t", "m", 0).levelName).isEqualTo("I")
        assertThat(LogLine(3, "t", "m", 0).levelName).isEqualTo("W")
        assertThat(LogLine(4, "t", "m", 0).levelName).isEqualTo("E")
        assertThat(LogLine(99, "t", "m", 0).levelName).isEqualTo("?")
    }

    @Test
    fun `SuPolicy enum has 3 values`() {
        assertThat(SuPolicy.values()).hasLength(3)
        assertThat(SuPolicy.valueOf("DENY")).isEqualTo(SuPolicy.DENY)
        assertThat(SuPolicy.valueOf("ALLOW")).isEqualTo(SuPolicy.ALLOW)
        assertThat(SuPolicy.valueOf("ALLOW_ONCE")).isEqualTo(SuPolicy.ALLOW_ONCE)
    }

    @Test
    fun `SuAppUi data class equality`() {
        val a = SuAppUi("com.example", 1000, SuPolicy.ALLOW, 0L, 0, 0)
        val b = SuAppUi("com.example", 1000, SuPolicy.ALLOW, 0L, 0, 0)
        assertThat(a).isEqualTo(b)
        assertThat(a.hashCode()).isEqualTo(b.hashCode())
    }

    @Test
    fun `SuLogEntryUi data class equality`() {
        val a = SuLogEntryUi(1234567L, "com.example", 1000, true, "su -c id")
        val b = SuLogEntryUi(1234567L, "com.example", 1000, true, "su -c id")
        assertThat(a).isEqualTo(b)
    }

    @Test
    fun `ModuleUi with empty capabilities`() {
        val m = ModuleUi(
            id = "empty", name = "Empty", version = "1.0", author = "t",
            description = "", enabled = false, priority = 0, capabilities = emptyList(),
        )
        assertThat(m.capabilities).isEmpty()
        assertThat(m.enabled).isFalse()
    }
}
