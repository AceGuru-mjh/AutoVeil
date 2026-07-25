package com.nexus.manager.ui.nav

/**
 * 导航路由常量
 *
 * 主页签为顶级路由；模块详情为子路由（带参数）。
 */
object Routes {
    const val DASHBOARD = "dashboard"
    const val MODULES = "modules"
    const val SUPERUSER = "superuser"
    const val LOGS = "logs"
    const val SETTINGS = "settings"

    const val MODULE_DETAIL = "module_detail/{moduleId}"

    fun moduleDetail(id: String) = "module_detail/$id"
}
