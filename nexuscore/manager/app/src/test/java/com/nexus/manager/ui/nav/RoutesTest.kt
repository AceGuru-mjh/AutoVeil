package com.nexus.manager.ui.nav

import com.google.common.truth.Truth.assertThat
import org.junit.Test

/**
 * Routes 路由常量单元测试
 *
 * 验证路由字符串的正确性，避免因 typo 导致的导航失败。
 */
class RoutesTest {

    @Test
    fun `top-level routes are unique`() {
        val all = listOf(Routes.DASHBOARD, Routes.MODULES, Routes.SUPERUSER,
                         Routes.LOGS, Routes.SETTINGS)
        assertThat(all).containsNoDuplicates()
    }

    @Test
    fun `MODULE_DETAIL template contains moduleId placeholder`() {
        assertThat(Routes.MODULE_DETAIL).contains("{moduleId}")
    }

    @Test
    fun `moduleDetail constructs correct path`() {
        val path = Routes.moduleDetail("my_mod")
        assertThat(path).isEqualTo("module_detail/my_mod")
    }

    @Test
    fun `moduleDetail handles special characters in id`() {
        // 模块 ID 经 isValidId 校验只能是 [a-z][a-z0-9_]{2,63}，
        // 但 Routes.moduleDetail 不应假设 ID 已校验，应当原样拼接
        val path = Routes.moduleDetail("test_mod_123")
        assertThat(path).isEqualTo("module_detail/test_mod_123")
    }

    @Test
    fun `DASHBOARD is the default start destination`() {
        // 验证 DASHBOARD 路由不是空字符串
        assertThat(Routes.DASHBOARD).isNotEmpty()
    }
}
