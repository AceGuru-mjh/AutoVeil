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

package com.nexus.manager.viewmodel

import androidx.lifecycle.SavedStateHandle
import androidx.lifecycle.ViewModel
import androidx.lifecycle.viewModelScope
import com.nexus.manager.data.model.ModuleUi
import com.nexus.manager.data.repo.NexusRepository
import kotlinx.coroutines.channels.Channel
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.flow.receiveAsFlow
import kotlinx.coroutines.launch

/**
 * 模块详情 ViewModel
 *
 * 接收 nav arg `moduleId`，从 Daemon 拉取模块列表后筛出对应模块。
 * 提供 启用/禁用/卸载 操作。
 */
class ModuleDetailViewModel(
    savedStateHandle: SavedStateHandle,
    private val repo: NexusRepository,
) : ViewModel() {

    val moduleId: String = checkNotNull(savedStateHandle["moduleId"])

    private val _module = MutableStateFlow<ModuleUi?>(null)
    val module: StateFlow<ModuleUi?> = _module.asStateFlow()

    private val _loading = MutableStateFlow(false)
    val loading: StateFlow<Boolean> = _loading.asStateFlow()

    private val _busy = MutableStateFlow(false)
    val busy: StateFlow<Boolean> = _busy.asStateFlow()

    private val _messages = Channel<String>(Channel.BUFFERED)
    val messages = _messages.receiveAsFlow()

    private var _uninstalled = MutableStateFlow(false)
    val uninstalled: StateFlow<Boolean> = _uninstalled

    init { refresh() }

    fun refresh() {
        viewModelScope.launch {
            _loading.value = true
            try {
                _module.value = repo.listModules().firstOrNull { it.id == moduleId }
                if (_module.value == null) {
                    _messages.trySend("未找到模块 $moduleId")
                }
            } catch (e: Exception) {
                _messages.trySend("加载失败：${e.message ?: "未知错误"}")
            } finally {
                _loading.value = false
            }
        }
    }

    fun toggleEnabled() {
        val current = _module.value ?: return
        if (_busy.value) return
        viewModelScope.launch {
            _busy.value = true
            val ok = runCatching {
                if (current.enabled) repo.disableModule(current.id)
                else repo.enableModule(current.id)
            }.getOrDefault(false)
            _messages.trySend(
                if (ok) "已${if (current.enabled) "禁用" else "启用"}"
                else "操作失败"
            )
            if (ok) refresh()
            _busy.value = false
        }
    }

    fun uninstall() {
        val current = _module.value ?: return
        if (_busy.value) return
        viewModelScope.launch {
            _busy.value = true
            val ok = runCatching { repo.uninstallModule(current.id) }.getOrDefault(false)
            _messages.trySend(if (ok) "${current.name} 已卸载" else "卸载失败")
            if (ok) _uninstalled.value = true
            _busy.value = false
        }
    }

    override fun onCleared() {
        super.onCleared()
        _messages.close()
    }
}
