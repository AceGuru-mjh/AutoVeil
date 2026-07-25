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
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.outlined.Bolt
import androidx.compose.material.icons.outlined.ChevronRight
import androidx.compose.material.icons.outlined.Insights
import androidx.compose.material.icons.outlined.PowerSettingsNew
import androidx.compose.material.icons.outlined.Refresh
import androidx.compose.material.icons.outlined.Security
import androidx.compose.material.icons.outlined.Shield
import androidx.compose.material.icons.outlined.VerifiedUser
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.CircularProgressIndicator
import androidx.compose.material3.DropdownMenu
import androidx.compose.material3.DropdownMenuItem
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import androidx.lifecycle.viewmodel.compose.viewModel
import com.nexus.manager.data.model.SystemStatus
import com.nexus.manager.ipc.NexusIpcClient
import com.nexus.manager.ipc.proto.RebootRequest
import com.nexus.manager.ui.components.GlassCard
import com.nexus.manager.ui.components.GlassTopBar
import com.nexus.manager.ui.components.CollectMessages
import com.nexus.manager.ui.components.InfoRow
import com.nexus.manager.ui.components.MetricCard
import com.nexus.manager.ui.components.SectionHeader
import com.nexus.manager.ui.components.StatusChip
import com.nexus.manager.ui.components.StatusDot
import com.nexus.manager.ui.components.StatusTone
import com.nexus.manager.ui.components.color
import com.nexus.manager.viewmodel.DashboardViewModel
import com.nexus.manager.viewmodel.NexusViewModelFactory

@Composable
fun DashboardPage(
    onNavigateToModules: () -> Unit,
    onNavigateToLogs: () -> Unit,
    viewModel: DashboardViewModel = viewModel(factory = NexusViewModelFactory()),
) {
    val status by viewModel.status.collectAsStateWithLifecycle()
    val connection by viewModel.connection.collectAsStateWithLifecycle()
    val refreshing by viewModel.refreshing.collectAsStateWithLifecycle()

    CollectMessages(viewModel.messages)

    var rebootMenuOpen by remember { mutableStateOf(false) }
    var safeModeDialog by remember { mutableStateOf(false) }

    Box(Modifier.fillMaxSize()) {
        Column(
            Modifier
                .fillMaxSize()
                .verticalScroll(rememberScrollState())
                .padding(bottom = 96.dp),
        ) {
            GlassTopBar(
                title = "状态总览",
                subtitle = "NexusCore Dashboard",
                actions = {
                    IconButton(onClick = viewModel::refresh, enabled = !refreshing) {
                        if (refreshing) {
                            CircularProgressIndicator(
                                modifier = Modifier.size(20.dp),
                                strokeWidth = 2.dp,
                            )
                        } else {
                            Icon(Icons.Outlined.Refresh, contentDescription = "刷新")
                        }
                    }
                    Box {
                        IconButton(onClick = { rebootMenuOpen = true }) {
                            Icon(Icons.Outlined.PowerSettingsNew, contentDescription = "重启")
                        }
                        DropdownMenu(
                            expanded = rebootMenuOpen,
                            onDismissRequest = { rebootMenuOpen = false },
                        ) {
                            RebootMenuItem("正常重启") { viewModel.reboot(RebootRequest.Mode.NORMAL); rebootMenuOpen = false }
                            RebootMenuItem("软重启 (userspace)") { viewModel.reboot(RebootRequest.Mode.USERSPACE); rebootMenuOpen = false }
                            RebootMenuItem("Recovery") { viewModel.reboot(RebootRequest.Mode.RECOVERY); rebootMenuOpen = false }
                            RebootMenuItem("Bootloader") { viewModel.reboot(RebootRequest.Mode.BOOTLOADER); rebootMenuOpen = false }
                            RebootMenuItem("Download") { viewModel.reboot(RebootRequest.Mode.DOWNLOAD); rebootMenuOpen = false }
                        }
                    }
                },
            )

            Spacer(Modifier.height(8.dp))

            // 连接状态横幅
            ConnectionBanner(connection = connection)

            // Hero 状态卡
            HeroStatusCard(
                status = status,
                onRestartDaemon = viewModel::restartDaemon,
                onSafeMode = { safeModeDialog = true },
            )

            Spacer(Modifier.height(16.dp))

            // 指标网格
            SectionHeader(title = "运行指标")
            MetricGrid(status = status, onNavigateToModules = onNavigateToModules)

            Spacer(Modifier.height(16.dp))

            // 系统环境
            SectionHeader(title = "系统环境")
            EnvironmentCard(status = status)

            Spacer(Modifier.height(16.dp))

            // 快捷入口
            SectionHeader(title = "快捷入口")
            QuickActionsCard(
                onModules = onNavigateToModules,
                onLogs = onNavigateToLogs,
            )
        }
    }

    if (safeModeDialog) {
        AlertDialog(
            onDismissRequest = { safeModeDialog = false },
            title = { Text("进入安全模式") },
            text = { Text("进入安全模式后，所有模块将在下次重启前不加载。确认继续？") },
            confirmButton = {
                TextButton(onClick = {
                    viewModel.enterSafeMode()
                    safeModeDialog = false
                }) { Text("确认") }
            },
            dismissButton = {
                TextButton(onClick = { safeModeDialog = false }) { Text("取消") }
            },
        )
    }
}

@Composable
private fun ConnectionBanner(connection: NexusIpcClient.Connection) {
    val (text, tone) = when (connection) {
        is NexusIpcClient.Connection.Connected -> "Daemon 已连接" to StatusTone.OK
        is NexusIpcClient.Connection.Reconnecting ->
            "正在重连… (第 ${connection.attempt} 次)" to StatusTone.WARN
        is NexusIpcClient.Connection.Failed ->
            "连接失败：${connection.cause.message ?: "未知"}" to StatusTone.ERROR
    }
    Box(Modifier.padding(horizontal = 16.dp, vertical = 4.dp)) {
        StatusChip(text = text, tone = tone)
    }
}

@Composable
private fun HeroStatusCard(
    status: SystemStatus,
    onRestartDaemon: () -> Unit,
    onSafeMode: () -> Unit,
) {
    GlassCard(
        modifier = Modifier
            .fillMaxWidth()
            .padding(horizontal = 16.dp),
        contentPadding = 20.dp,
    ) {
        Column {
            Row(verticalAlignment = Alignment.CenterVertically) {
                Icon(
                    Icons.Outlined.Shield,
                    contentDescription = null,
                    tint = MaterialTheme.colorScheme.primary,
                    modifier = Modifier.size(32.dp),
                )
                Spacer(Modifier.width(12.dp))
                Column(Modifier.weight(1f)) {
                    Text(
                        "NexusCore",
                        style = MaterialTheme.typography.headlineSmall,
                        fontWeight = FontWeight.SemiBold,
                    )
                    Text(
                        "Daemon ${status.daemonVersion}",
                        style = MaterialTheme.typography.bodySmall,
                        color = MaterialTheme.colorScheme.onSurfaceVariant,
                    )
                }
                StatusChip(
                    text = if (status.safeMode) "安全模式" else "运行中",
                    tone = when {
                        status.safeMode -> StatusTone.WARN
                        status.daemonRunning -> StatusTone.OK
                        else -> StatusTone.ERROR
                    },
                )
            }
            Spacer(Modifier.height(16.dp))
            Row(verticalAlignment = Alignment.CenterVertically) {
                StatusDot(
                    text = "Root ${status.rootProvider} ${status.rootVersion}",
                    tone = if (status.rootAvailable) StatusTone.OK else StatusTone.ERROR,
                    modifier = Modifier.weight(1f),
                )
                StatusDot(
                    text = if (status.selinuxEnforcing) "SELinux Enforcing" else "SELinux Permissive",
                    tone = if (status.selinuxEnforcing) StatusTone.OK else StatusTone.WARN,
                )
            }
            Spacer(Modifier.height(12.dp))
            Row {
                ActionChipMini("重启 Daemon", Icons.Outlined.Refresh, onRestartDaemon)
                Spacer(Modifier.width(8.dp))
                ActionChipMini("安全模式", Icons.Outlined.Security, onSafeMode, tone = StatusTone.WARN)
            }
        }
    }
}

@Composable
private fun ActionChipMini(
    label: String,
    icon: ImageVector,
    onClick: () -> Unit,
    tone: StatusTone = StatusTone.NEUTRAL,
) {
    androidx.compose.material3.AssistChip(
        onClick = onClick,
        label = { Text(label, style = MaterialTheme.typography.labelMedium) },
        leadingIcon = { Icon(icon, contentDescription = label, modifier = Modifier.size(16.dp)) },
        colors = androidx.compose.material3.AssistChipDefaults.assistChipColors(
            leadingIconContentColor = tone.color(),
        ),
    )
}

@Composable
private fun MetricGrid(status: SystemStatus, onNavigateToModules: () -> Unit) {
    val items = listOf(
        MetricItem("已安装模块", "${status.moduleCount}", StatusTone.INFO, Icons.Outlined.VerifiedUser),
        MetricItem("文件系统", status.fsInterceptor, StatusTone.NEUTRAL, Icons.Outlined.Insights),
        MetricItem("Daemon PID", "${status.daemonPid}", StatusTone.NEUTRAL, Icons.Outlined.Bolt),
        MetricItem("运行时长", formatUptimeShort(status.uptimeMs), StatusTone.NEUTRAL, Icons.Outlined.Refresh),
    )
    // 用 LazyVerticalGrid 嵌在 verticalScroll 中需要固定高度，这里改用两行 Row
    Column(Modifier.padding(horizontal = 16.dp)) {
        items.chunked(2).forEach { row ->
            Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(12.dp)) {
                row.forEach { item ->
                    MetricCard(
                        label = item.label,
                        value = item.value,
                        tone = item.tone,
                        modifier = Modifier.weight(1f),
                    )
                }
                if (row.size == 1) Spacer(Modifier.weight(1f))
            }
            Spacer(Modifier.height(12.dp))
        }
    }
    // 模块数卡片可点击进入模块页
}

private data class MetricItem(
    val label: String,
    val value: String,
    val tone: StatusTone,
    val icon: ImageVector,
)

private fun formatUptimeShort(ms: Long): String {
    if (ms <= 0) return "—"
    val sec = ms / 1000
    val d = sec / 86400
    val h = (sec % 86400) / 3600
    val m = (sec % 3600) / 60
    return when {
        d > 0 -> "${d}d ${h}h"
        h > 0 -> "${h}h ${m}m"
        else -> "${m}m"
    }
}

@Composable
private fun EnvironmentCard(status: SystemStatus) {
    GlassCard(
        modifier = Modifier
            .fillMaxWidth()
            .padding(horizontal = 16.dp),
        contentPadding = 16.dp,
    ) {
        Column {
            InfoRow(label = "Android 版本", value = status.androidVersion)
            InfoRow(label = "安全补丁", value = status.securityPatch)
            InfoRow(label = "内核版本", value = status.kernelVersion)
            InfoRow(label = "SELinux 域", value = status.selinuxDomain)
            InfoRow(label = "架构", value = status.arch)
        }
    }
}

@Composable
private fun QuickActionsCard(
    onModules: () -> Unit,
    onLogs: () -> Unit,
) {
    GlassCard(
        modifier = Modifier
            .fillMaxWidth()
            .padding(horizontal = 16.dp),
        contentPadding = 8.dp,
    ) {
        Column {
            ActionRow("模块管理", "查看 / 启用 / 安装模块", Icons.Outlined.VerifiedUser, onModules)
            ActionRow("实时日志", "查看 Daemon 与 SU 日志流", Icons.Outlined.Insights, onLogs)
        }
    }
}

@Composable
private fun ActionRow(title: String, subtitle: String, icon: ImageVector, onClick: () -> Unit) {
    Row(
        Modifier
            .fillMaxWidth()
            .clickable(onClick = onClick)
            .padding(12.dp),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        Icon(icon, contentDescription = null, tint = MaterialTheme.colorScheme.primary)
        Spacer(Modifier.width(16.dp))
        Column(Modifier.weight(1f)) {
            Text(title, style = MaterialTheme.typography.titleSmall)
            Text(subtitle, style = MaterialTheme.typography.bodySmall, color = MaterialTheme.colorScheme.onSurfaceVariant)
        }
        Icon(
            Icons.Outlined.ChevronRight,
            contentDescription = null,
            tint = MaterialTheme.colorScheme.onSurfaceVariant,
            modifier = Modifier.size(20.dp),
        )
    }
}

@Composable
private fun RebootMenuItem(label: String, onClick: () -> Unit) {
    DropdownMenuItem(text = { Text(label) }, onClick = onClick)
}
