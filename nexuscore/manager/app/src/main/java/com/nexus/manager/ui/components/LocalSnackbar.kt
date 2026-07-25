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

package com.nexus.manager.ui.components

import androidx.compose.material3.SnackbarHostState
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.compositionLocalOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.staticCompositionLocalOf
import kotlinx.coroutines.flow.Flow

/**
 * 全局 Snackbar 控制入口
 *
 * 由 [com.nexus.manager.ui.NexusRoot] 提供，页面/VM 通过它显示短暂消息。
 * 用法：
 *   val snackbar = LocalSnackbar.current
 *   snackbar.show("已启用")
 */
val LocalSnackbar = staticCompositionLocalOf<SnackbarController> {
    error("LocalSnackbar not provided")
}

/** Snackbar 控制器：包装 SnackbarHostState，避免在业务层暴露 host 细节 */
class SnackbarController(private val host: SnackbarHostState) {
    suspend fun show(message: String, actionLabel: String? = null) {
        host.showSnackbar(message = message, actionLabel = actionLabel, withDismissAction = actionLabel == null)
    }
}

/**
 * 收集 VM 的 messages 流并自动展示为 Snackbar。
 * 在页面顶部调用一次即可。
 */
@Composable
fun CollectMessages(messages: Flow<String>) {
    val snackbar = LocalSnackbar.current
    LaunchedEffect(messages) {
        messages.collect { snackbar.show(it) }
    }
}
