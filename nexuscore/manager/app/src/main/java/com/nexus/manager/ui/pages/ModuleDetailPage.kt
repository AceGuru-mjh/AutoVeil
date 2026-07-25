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

package com.nexus.manager.ui.pages

import android.content.Intent
import android.net.Uri
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.FlowRow
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.layout.ExperimentalLayoutApi
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.outlined.ArrowBack
import androidx.compose.material.icons.outlined.DeleteOutline
import androidx.compose.material.icons.outlined.Extension
import androidx.compose.material.icons.outlined.OpenInNew
import androidx.compose.material.icons.outlined.PowerSettingsNew
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.AssistChip
import androidx.compose.material3.AssistChipDefaults
import androidx.compose.material3.Button
import androidx.compose.material3.ButtonDefaults
import androidx.compose.material3.CircularProgressIndicator
import androidx.compose.material3.Icon
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import androidx.lifecycle.viewmodel.compose.viewModel
import com.nexus.manager.data.model.ModuleUi
import com.nexus.manager.ui.components.GlassCard
import com.nexus.manager.ui.components.GlassTopBar
import com.nexus.manager.ui.components.CollectMessages
import com.nexus.manager.ui.components.InfoRow
import com.nexus.manager.ui.components.SectionHeader
import com.nexus.manager.ui.components.StatusChip
import com.nexus.manager.ui.components.StatusTone
import com.nexus.manager.viewmodel.ModuleDetailViewModel
import com.nexus.manager.viewmodel.NexusViewModelFactory

@OptIn(ExperimentalLayoutApi::class)
@Composable
fun ModuleDetailPage(
    onBack: () -> Unit,
    viewModel: ModuleDetailViewModel = viewModel(factory = NexusViewModelFactory()),
) {
    val module by viewModel.module.collectAsStateWithLifecycle()
    val loading by viewModel.loading.collectAsStateWithLifecycle()
    val busy by viewModel.busy.collectAsStateWithLifecycle()
    val uninstalled by viewModel.uninstalled.collectAsStateWithLifecycle()
    val context = LocalContext.current

    CollectMessages(viewModel.messages)

    var uninstallDialog by remember { mutableStateOf(false) }

    // 卸载成功后自动返回
    LaunchedEffect(uninstalled) {
        if (uninstalled) onBack()
    }

    Box(Modifier.fillMaxSize()) {
        Column(
            Modifier
                .fillMaxSize()
                .verticalScroll(rememberScrollState())
                .padding(bottom = 96.dp),
        ) {
            GlassTopBar(
                title = module?.name ?: "模块详情",
                subtitle = module?.id,
                navigationIcon = Icons.AutoMirrored.Outlined.ArrowBack,
                onNavigationClick = onBack,
            )

            if (loading && module == null) {
                Box(Modifier.fillMaxSize(), contentAlignment = Alignment.Center) {
                    CircularProgressIndicator()
                }
                return@Column
            }

            val m = module
            if (m == null) {
                Box(Modifier.fillMaxSize(), contentAlignment = Alignment.Center) {
                    Text("模块不存在", color = MaterialTheme.colorScheme.onSurfaceVariant)
                }
                return@Column
            }

            Spacer(Modifier.height(8.dp))

            // 头部卡
            GlassCard(
                modifier = Modifier
                    .fillMaxWidth()
                    .padding(horizontal = 16.dp),
                contentPadding = 20.dp,
            ) {
                Column {
                    Row(verticalAlignment = Alignment.CenterVertically) {
                        Icon(
                            Icons.Outlined.Extension,
                            contentDescription = null,
                            tint = MaterialTheme.colorScheme.primary,
                            modifier = Modifier.size(36.dp),
                        )
                        Spacer(Modifier.width(12.dp))
                        Column(Modifier.weight(1f)) {
                            Text(m.name, style = MaterialTheme.typography.titleLarge, fontWeight = FontWeight.SemiBold)
                            Text("v${m.version}", style = MaterialTheme.typography.bodyMedium, color = MaterialTheme.colorScheme.onSurfaceVariant)
                        }
                        StatusChip(
                            text = if (m.enabled) "已启用" else "已禁用",
                            tone = if (m.enabled) StatusTone.OK else StatusTone.NEUTRAL,
                        )
                    }
                    if (m.description.isNotEmpty()) {
                        Spacer(Modifier.height(12.dp))
                        Text(m.description, style = MaterialTheme.typography.bodyMedium)
                    }
                    if (m.capabilities.isNotEmpty()) {
                        Spacer(Modifier.height(12.dp))
                        FlowRow(horizontalArrangement = Arrangement.spacedBy(6.dp)) {
                            m.capabilities.forEach { cap ->
                                AssistChip(
                                    onClick = {},
                                    label = { Text(cap, style = MaterialTheme.typography.labelSmall) },
                                    colors = AssistChipDefaults.assistChipColors(
                                        containerColor = MaterialTheme.colorScheme.secondaryContainer,
                                        labelColor = MaterialTheme.colorScheme.onSecondaryContainer,
                                    ),
                                )
                            }
                        }
                    }
                }
            }

            Spacer(Modifier.height(16.dp))

            // 元信息
            SectionHeader(title = "元信息", modifier = Modifier.padding(horizontal = 16.dp))
            GlassCard(
                modifier = Modifier
                    .fillMaxWidth()
                    .padding(horizontal = 16.dp),
                contentPadding = 16.dp,
            ) {
                Column {
                    InfoRow(label = "模块 ID", value = m.id)
                    InfoRow(label = "作者", value = m.author.ifEmpty { "—" })
                    InfoRow(label = "版本", value = m.version)
                    InfoRow(label = "优先级", value = "${m.priority}")
                    InfoRow(label = "可更新", value = if (m.hasUpdate) "是" else "否")
                }
            }

            Spacer(Modifier.height(16.dp))

            // 操作
            SectionHeader(title = "操作", modifier = Modifier.padding(horizontal = 16.dp))
            GlassCard(
                modifier = Modifier
                    .fillMaxWidth()
                    .padding(horizontal = 16.dp),
                contentPadding = 16.dp,
            ) {
                Column {
                    Button(
                        onClick = viewModel::toggleEnabled,
                        enabled = !busy,
                        modifier = Modifier.fillMaxWidth(),
                    ) {
                        Icon(Icons.Outlined.PowerSettingsNew, contentDescription = null)
                        Spacer(Modifier.width(8.dp))
                        Text(if (m.enabled) "禁用模块" else "启用模块")
                    }
                    Spacer(Modifier.height(8.dp))
                    if (m.hasUpdate && m.updateUrl.isNotEmpty()) {
                        OutlinedButton(
                            onClick = {
                                runCatching {
                                    context.startActivity(
                                        Intent(Intent.ACTION_VIEW, Uri.parse(m.updateUrl))
                                    )
                                }
                            },
                            modifier = Modifier.fillMaxWidth(),
                        ) {
                            Icon(Icons.Outlined.OpenInNew, contentDescription = null)
                            Spacer(Modifier.width(8.dp))
                            Text("打开更新地址")
                        }
                        Spacer(Modifier.height(8.dp))
                    }
                    Button(
                        onClick = { uninstallDialog = true },
                        enabled = !busy,
                        modifier = Modifier.fillMaxWidth(),
                        colors = ButtonDefaults.buttonColors(
                            containerColor = MaterialTheme.colorScheme.errorContainer,
                            contentColor = MaterialTheme.colorScheme.onErrorContainer,
                        ),
                    ) {
                        Icon(Icons.Outlined.DeleteOutline, contentDescription = null)
                        Spacer(Modifier.width(8.dp))
                        Text("卸载模块")
                    }
                }
            }
        }
    }

    if (uninstallDialog) {
        AlertDialog(
            onDismissRequest = { uninstallDialog = false },
            title = { Text("卸载模块") },
            text = { Text("确认卸载 ${module?.name}？此操作不可撤销，重启后生效。") },
            confirmButton = {
                TextButton(onClick = {
                    viewModel.uninstall()
                    uninstallDialog = false
                }) { Text("卸载", color = MaterialTheme.colorScheme.error) }
            },
            dismissButton = {
                TextButton(onClick = { uninstallDialog = false }) { Text("取消") }
            },
        )
    }
}
