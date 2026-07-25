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
