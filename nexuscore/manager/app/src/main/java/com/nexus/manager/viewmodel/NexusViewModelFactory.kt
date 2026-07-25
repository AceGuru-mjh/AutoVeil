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

package com.nexus.manager.viewmodel

import androidx.lifecycle.SavedStateHandle
import androidx.lifecycle.ViewModel
import androidx.lifecycle.ViewModelProvider
import androidx.lifecycle.createSavedStateHandle
import androidx.lifecycle.viewmodel.CreationExtras
import com.nexus.manager.NexusApp

/**
 * 统一 ViewModel 工厂
 *
 * 从 [NexusApp] 取 Repository / Settings 注入到各 ViewModel。
 * 支持 [SavedStateHandle]（用于 ModuleDetailViewModel 读取 nav arg）。
 *
 * 使用现代 CreationExtras API：Compose 的 `viewModel()` 会自动注入
 * SavedStateRegistryOwner 与 nav args，因此 `extras.createSavedStateHandle()`
 * 可正确还原 nav 参数。
 *
 * 用法：
 *   viewModel<DashboardViewModel>(factory = NexusViewModelFactory())
 *   viewModel<ModuleDetailViewModel>(factory = NexusViewModelFactory())
 */
class NexusViewModelFactory : ViewModelProvider.Factory {

    @Suppress("UNCHECKED_CAST")
    override fun <T : ViewModel> create(modelClass: Class<T>, extras: CreationExtras): T {
        val app = NexusApp.get()
        return when {
            modelClass.isAssignableFrom(DashboardViewModel::class.java) ->
                DashboardViewModel(app.repository) as T
            modelClass.isAssignableFrom(ModulesViewModel::class.java) ->
                ModulesViewModel(app.repository) as T
            modelClass.isAssignableFrom(SuperUserViewModel::class.java) ->
                SuperUserViewModel(app.repository) as T
            modelClass.isAssignableFrom(LogsViewModel::class.java) ->
                LogsViewModel(app.repository) as T
            modelClass.isAssignableFrom(SettingsViewModel::class.java) ->
                SettingsViewModel(app.settings, app.repository) as T
            modelClass.isAssignableFrom(ModuleDetailViewModel::class.java) -> {
                val handle: SavedStateHandle = extras.createSavedStateHandle()
                ModuleDetailViewModel(handle, app.repository) as T
            }
            modelClass.isAssignableFrom(SuRequestViewModel::class.java) ->
                SuRequestViewModel(app.repository) as T
            else -> throw IllegalArgumentException("unknown VM: ${modelClass.name}")
        }
    }
}
