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

package com.nexus.manager.ui.pages

import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.outlined.Brightness6
import androidx.compose.material.icons.outlined.DeleteForever
import androidx.compose.material.icons.outlined.Fingerprint
import androidx.compose.material.icons.outlined.Info
import androidx.compose.material.icons.outlined.Palette
import androidx.compose.material.icons.outlined.SystemUpdate
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.Icon
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.RadioButton
import androidx.compose.material3.Switch
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import androidx.lifecycle.viewmodel.compose.viewModel
import com.nexus.manager.BuildConfig
import com.nexus.manager.data.settings.UpdateChannel
import com.nexus.manager.ui.components.GlassCard
import com.nexus.manager.ui.components.GlassTopBar
import com.nexus.manager.ui.components.CollectMessages
import com.nexus.manager.ui.components.SectionHeader
import com.nexus.manager.ui.theme.ThemeMode
import com.nexus.manager.viewmodel.NexusViewModelFactory
import com.nexus.manager.viewmodel.SettingsViewModel

@Composable
fun SettingsPage(
    viewModel: SettingsViewModel = viewModel(factory = NexusViewModelFactory()),
) {
    val state by viewModel.state.collectAsStateWithLifecycle()
    var uninstallDialog by remember { mutableStateOf(false) }
    var themeDialog by remember { mutableStateOf(false) }
    var channelDialog by remember { mutableStateOf(false) }

    CollectMessages(viewModel.messages)

    Column(
        Modifier
            .fillMaxSize()
            .verticalScroll(rememberScrollState()),
    ) {
        GlassTopBar(title = "设置", subtitle = "NexusCore Manager")

        Spacer(Modifier.height(8.dp))

        // 主题
        SectionHeader(title = "外观", modifier = Modifier.padding(horizontal = 16.dp))
        GlassCard(
            modifier = Modifier
                .fillMaxWidth()
                .padding(horizontal = 16.dp),
            contentPadding = 0.dp,
        ) {
            SettingRow(
                icon = Icons.Outlined.Brightness6,
                title = "主题模式",
                subtitle = themeModeLabel(state.themeMode),
                onClick = { themeDialog = true },
            )
            SettingRow(
                icon = Icons.Outlined.Palette,
                title = "动态颜色",
                subtitle = "跟随系统壁纸取色（Android 12+）",
                trailing = {
                    Switch(
                        checked = state.dynamicColor,
                        onCheckedChange = viewModel::setDynamicColor,
                    )
                },
            )
        }

        Spacer(Modifier.height(16.dp))

        // 安全
        SectionHeader(title = "安全", modifier = Modifier.padding(horizontal = 16.dp))
        GlassCard(
            modifier = Modifier
                .fillMaxWidth()
                .padding(horizontal = 16.dp),
            contentPadding = 0.dp,
        ) {
            SettingRow(
                icon = Icons.Outlined.Fingerprint,
                title = "生物认证",
                subtitle = "进入敏感操作时要求指纹 / 面部（占位）",
                trailing = {
                    Switch(
                        checked = state.biometricEnabled,
                        onCheckedChange = viewModel::setBiometricEnabled,
                    )
                },
            )
        }

        Spacer(Modifier.height(16.dp))

        // 更新
        SectionHeader(title = "更新", modifier = Modifier.padding(horizontal = 16.dp))
        GlassCard(
            modifier = Modifier
                .fillMaxWidth()
                .padding(horizontal = 16.dp),
            contentPadding = 0.dp,
        ) {
            SettingRow(
                icon = Icons.Outlined.SystemUpdate,
                title = "更新通道",
                subtitle = updateChannelLabel(state.updateChannel),
                onClick = { channelDialog = true },
            )
            SettingRow(
                icon = Icons.Outlined.Info,
                title = "检查更新",
                subtitle = "立即检查框架新版本",
                onClick = viewModel::checkUpdate,
            )
        }

        Spacer(Modifier.height(16.dp))

        // 日志
        SectionHeader(title = "日志", modifier = Modifier.padding(horizontal = 16.dp))
        GlassCard(
            modifier = Modifier
                .fillMaxWidth()
                .padding(horizontal = 16.dp),
            contentPadding = 0.dp,
        ) {
            LogLevelRow(current = state.logMinLevel, onSelect = viewModel::setLogMinLevel)
        }

        Spacer(Modifier.height(16.dp))

        // 关于
        SectionHeader(title = "关于", modifier = Modifier.padding(horizontal = 16.dp))
        GlassCard(
            modifier = Modifier
                .fillMaxWidth()
                .padding(horizontal = 16.dp),
            contentPadding = 16.dp,
        ) {
            Column {
                AboutRow(label = "Manager 版本", value = state.appVersion)
                AboutRow(label = "Daemon 版本", value = state.daemonVersion)
                AboutRow(label = "构建类型", value = BuildConfig.BUILD_TYPE)
            }
        }

        Spacer(Modifier.height(16.dp))

        // 危险区
        SectionHeader(title = "危险操作", modifier = Modifier.padding(horizontal = 16.dp))
        GlassCard(
            modifier = Modifier
                .fillMaxWidth()
                .padding(horizontal = 16.dp),
            contentPadding = 0.dp,
        ) {
            SettingRow(
                icon = Icons.Outlined.DeleteForever,
                title = "卸载 NexusCore 框架",
                subtitle = "移除 Daemon 与所有模块，重启后生效",
                onClick = { uninstallDialog = true },
                danger = true,
            )
        }

        Spacer(Modifier.height(96.dp))
    }

    if (themeDialog) {
        AlertDialog(
            onDismissRequest = { themeDialog = false },
            title = { Text("主题模式") },
            text = {
                Column {
                    ThemeMode.entries.forEach { mode ->
                        Row(
                            Modifier
                                .fillMaxWidth()
                                .clickable { viewModel.setThemeMode(mode); themeDialog = false }
                                .padding(vertical = 8.dp),
                            verticalAlignment = Alignment.CenterVertically,
                        ) {
                            RadioButton(
                                selected = state.themeMode == mode,
                                onClick = { viewModel.setThemeMode(mode); themeDialog = false },
                            )
                            Spacer(Modifier.width(8.dp))
                            Text(themeModeLabel(mode))
                        }
                    }
                }
            },
            confirmButton = {
                TextButton(onClick = { themeDialog = false }) { Text("关闭") }
            },
        )
    }

    if (channelDialog) {
        AlertDialog(
            onDismissRequest = { channelDialog = false },
            title = { Text("更新通道") },
            text = {
                Column {
                    UpdateChannel.entries.forEach { ch ->
                        Row(
                            Modifier
                                .fillMaxWidth()
                                .clickable { viewModel.setUpdateChannel(ch); channelDialog = false }
                                .padding(vertical = 8.dp),
                            verticalAlignment = Alignment.CenterVertically,
                        ) {
                            RadioButton(
                                selected = state.updateChannel == ch,
                                onClick = { viewModel.setUpdateChannel(ch); channelDialog = false },
                            )
                            Spacer(Modifier.width(8.dp))
                            Text(updateChannelLabel(ch))
                        }
                    }
                }
            },
            confirmButton = {
                TextButton(onClick = { channelDialog = false }) { Text("关闭") }
            },
        )
    }

    if (uninstallDialog) {
        AlertDialog(
            onDismissRequest = { uninstallDialog = false },
            title = { Text("卸载 NexusCore") },
            text = {
                Text("此操作将移除 Daemon 与所有模块，不可撤销。确认卸载？")
            },
            confirmButton = {
                TextButton(onClick = {
                    viewModel.uninstallFramework()
                    uninstallDialog = false
                }) { Text("卸载", color = MaterialTheme.colorScheme.error) }
            },
            dismissButton = {
                TextButton(onClick = { uninstallDialog = false }) { Text("取消") }
            },
        )
    }
}

@Composable
private fun SettingRow(
    icon: androidx.compose.ui.graphics.vector.ImageVector,
    title: String,
    subtitle: String,
    onClick: (() -> Unit)? = null,
    trailing: @Composable (() -> Unit)? = null,
    danger: Boolean = false,
) {
    Row(
        Modifier
            .fillMaxWidth()
            .then(if (onClick != null) Modifier.clickable(onClick = onClick) else Modifier)
            .padding(16.dp),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        Icon(
            icon,
            contentDescription = null,
            tint = if (danger) MaterialTheme.colorScheme.error else MaterialTheme.colorScheme.primary,
        )
        Spacer(Modifier.width(16.dp))
        Column(Modifier.weight(1f)) {
            Text(
                title,
                style = MaterialTheme.typography.titleSmall,
                color = if (danger) MaterialTheme.colorScheme.error else MaterialTheme.colorScheme.onSurface,
            )
            Text(
                subtitle,
                style = MaterialTheme.typography.bodySmall,
                color = MaterialTheme.colorScheme.onSurfaceVariant,
            )
        }
        if (trailing != null) trailing()
    }
}

@Composable
private fun LogLevelRow(current: Int, onSelect: (Int) -> Unit) {
    Column(Modifier.padding(16.dp)) {
        Text(
            "默认日志级别",
            style = MaterialTheme.typography.titleSmall,
        )
        Spacer(Modifier.height(8.dp))
        Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
            levelOptions.forEach { (level, label) ->
                Row(verticalAlignment = Alignment.CenterVertically) {
                    RadioButton(selected = current == level, onClick = { onSelect(level) })
                    Text(label, style = MaterialTheme.typography.bodyMedium)
                }
            }
        }
    }
}

@Composable
private fun AboutRow(label: String, value: String) {
    Row(
        Modifier
            .fillMaxWidth()
            .padding(vertical = 6.dp),
        horizontalArrangement = Arrangement.SpaceBetween,
    ) {
        Text(label, style = MaterialTheme.typography.bodyMedium, color = MaterialTheme.colorScheme.onSurfaceVariant)
        Text(value, style = MaterialTheme.typography.bodyMedium, fontWeight = FontWeight.Medium)
    }
}

private fun themeModeLabel(mode: ThemeMode) = when (mode) {
    ThemeMode.SYSTEM -> "跟随系统"
    ThemeMode.LIGHT -> "浅色"
    ThemeMode.DARK -> "深色"
}

private fun updateChannelLabel(ch: UpdateChannel) = when (ch) {
    UpdateChannel.STABLE -> "稳定版（推荐）"
    UpdateChannel.BETA -> "测试版"
    UpdateChannel.CANARY -> "Canary（每日构建）"
}

private val levelOptions = listOf(
    0 to "V", 1 to "D", 2 to "I", 3 to "W", 4 to "E",
)
