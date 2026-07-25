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

import androidx.lifecycle.ViewModel
import androidx.lifecycle.viewModelScope
import com.nexus.manager.data.repo.NexusRepository
import com.nexus.manager.data.settings.SettingsStore
import com.nexus.manager.data.settings.UpdateChannel
import com.nexus.manager.ui.theme.ThemeMode
import kotlinx.coroutines.channels.Channel
import kotlinx.coroutines.flow.SharingStarted
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.combine
import kotlinx.coroutines.flow.receiveAsFlow
import kotlinx.coroutines.flow.stateIn
import kotlinx.coroutines.launch

/**
 * 设置 ViewModel
 *
 * 职责：
 * - 主题模式（系统/浅色/深色）+ 动态颜色开关
 * - 日志最低级别
 * - 生物认证开关
 * - 框架更新通道（稳定/Beta/Canary）
 * - 检查框架更新（占位）
 * - 卸载 NexusCore 框架
 * - 关于信息（版本号 / Daemon 版本）
 */
class SettingsViewModel(
    private val settings: SettingsStore,
    private val repo: NexusRepository,
) : ViewModel() {

    data class SettingsUiState(
        val themeMode: ThemeMode = ThemeMode.SYSTEM,
        val dynamicColor: Boolean = false,
        val logMinLevel: Int = 2,
        val biometricEnabled: Boolean = false,
        val updateChannel: UpdateChannel = UpdateChannel.STABLE,
        val appVersion: String = "1.0.0",
        val daemonVersion: String = "—",
    )

    val state: StateFlow<SettingsUiState> = combine(
        settings.themeMode,
        settings.dynamicColor,
        settings.logMinLevel,
        settings.biometricEnabled,
        settings.updateChannel,
    ) { mode, dyn, level, bio, channel ->
        SettingsUiState(
            themeMode = mode, dynamicColor = dyn, logMinLevel = level,
            biometricEnabled = bio, updateChannel = channel,
        )
    }.stateIn(
        scope = viewModelScope,
        started = SharingStarted.WhileSubscribed(5_000),
        initialValue = SettingsUiState(),
    )

    private val _messages = Channel<String>(Channel.BUFFERED)
    val messages = _messages.receiveAsFlow()

    fun setThemeMode(mode: ThemeMode) {
        viewModelScope.launch { settings.setThemeMode(mode) }
    }

    fun setDynamicColor(enabled: Boolean) {
        viewModelScope.launch { settings.setDynamicColor(enabled) }
    }

    fun setLogMinLevel(level: Int) {
        viewModelScope.launch { settings.setLogMinLevel(level) }
    }

    fun setBiometricEnabled(enabled: Boolean) {
        viewModelScope.launch {
            settings.setBiometricEnabled(enabled)
            _messages.trySend(if (enabled) "已开启生物认证" else "已关闭生物认证")
        }
    }

    fun setUpdateChannel(channel: UpdateChannel) {
        viewModelScope.launch { settings.setUpdateChannel(channel) }
    }

    /** 占位：检查框架更新。当前无网络层，仅返回提示。 */
    fun checkUpdate() {
        viewModelScope.launch {
            _messages.trySend("当前版本暂未实现远程更新通道，请手动到 GitHub 下载最新 Release")
        }
    }

    fun uninstallFramework() {
        viewModelScope.launch {
            val ok = runCatching { repo.uninstallFramework() }.getOrDefault(false)
            _messages.trySend(if (ok) "框架卸载请求已发送，重启后生效" else "卸载失败")
        }
    }

    override fun onCleared() {
        super.onCleared()
        _messages.close()
    }
}

