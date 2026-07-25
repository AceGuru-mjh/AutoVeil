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
