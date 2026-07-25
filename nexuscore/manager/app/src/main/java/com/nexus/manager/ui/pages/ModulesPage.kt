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

import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
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
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.outlined.Add
import androidx.compose.material.icons.outlined.ChevronRight
import androidx.compose.material.icons.outlined.Close
import androidx.compose.material.icons.outlined.Extension
import androidx.compose.material.icons.outlined.Refresh
import androidx.compose.material.icons.outlined.Search
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.CircularProgressIndicator
import androidx.compose.material3.ExtendedFloatingActionButton
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.LinearProgressIndicator
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedTextField
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
import androidx.compose.ui.draw.clip
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import androidx.lifecycle.viewmodel.compose.viewModel
import com.nexus.manager.data.bridge.FileBridge
import com.nexus.manager.data.model.ModuleUi
import com.nexus.manager.ui.components.GlassCard
import com.nexus.manager.ui.components.GlassTopBar
import com.nexus.manager.ui.components.CollectMessages
import com.nexus.manager.ui.components.StatusChip
import com.nexus.manager.ui.components.StatusTone
import com.nexus.manager.viewmodel.ModulesViewModel
import com.nexus.manager.viewmodel.NexusViewModelFactory

@Composable
fun ModulesPage(
    onOpenDetail: (String) -> Unit,
    viewModel: ModulesViewModel = viewModel(factory = NexusViewModelFactory()),
) {
    val modules by viewModel.modules.collectAsStateWithLifecycle()
    val loading by viewModel.loading.collectAsStateWithLifecycle()
    val installing by viewModel.installing.collectAsStateWithLifecycle()
    val busyIds by viewModel.busyIds.collectAsStateWithLifecycle()
    val context = LocalContext.current
    var query by remember { mutableStateOf("") }
    var searchOpen by remember { mutableStateOf(false) }

    CollectMessages(viewModel.messages)

    val filtered = remember(modules, query) {
        if (query.isBlank()) modules
        else modules.filter {
            it.name.contains(query, ignoreCase = true) ||
                it.id.contains(query, ignoreCase = true) ||
                it.author.contains(query, ignoreCase = true) ||
                it.description.contains(query, ignoreCase = true)
        }
    }

    val zipPicker = rememberLauncherForActivityResult(
        contract = ActivityResultContracts.OpenDocument(),
    ) { uri ->
        if (uri != null) {
            val path = FileBridge.copyUriToTemp(context, uri)
            if (path != null) {
                viewModel.installFromZip(path)
            } else {
                android.widget.Toast.makeText(context, "无法读取 ZIP 文件", android.widget.Toast.LENGTH_SHORT).show()
            }
        }
    }

    Box(Modifier.fillMaxSize()) {
        Column(Modifier.fillMaxSize()) {
            GlassTopBar(
                title = "模块",
                subtitle = "已安装 ${modules.size} 个${if (query.isNotBlank() && filtered.size != modules.size) " · 命中 ${filtered.size}" else ""}",
                actions = {
                    IconButton(onClick = {
                        if (searchOpen) { searchOpen = false; query = "" }
                        else searchOpen = true
                    }) {
                        Icon(
                            if (searchOpen) Icons.Outlined.Close else Icons.Outlined.Search,
                            contentDescription = if (searchOpen) "关闭搜索" else "搜索",
                            tint = if (searchOpen) MaterialTheme.colorScheme.primary
                            else MaterialTheme.colorScheme.onSurface,
                        )
                    }
                    IconButton(onClick = viewModel::refresh, enabled = !loading) {
                        if (loading) {
                            CircularProgressIndicator(modifier = Modifier.size(20.dp), strokeWidth = 2.dp)
                        } else {
                            Icon(Icons.Outlined.Refresh, contentDescription = "刷新")
                        }
                    }
                },
            )

            if (searchOpen) {
                OutlinedTextField(
                    value = query,
                    onValueChange = { query = it },
                    modifier = Modifier
                        .fillMaxWidth()
                        .padding(horizontal = 16.dp, vertical = 4.dp),
                    placeholder = { Text("搜索名称 / ID / 作者…") },
                    leadingIcon = { Icon(Icons.Outlined.Search, contentDescription = null) },
                    singleLine = true,
                    shape = RoundedCornerShape(16.dp),
                )
            }

            if (installing) {
                LinearProgressIndicator(
                    modifier = Modifier
                        .fillMaxWidth()
                        .padding(horizontal = 16.dp, vertical = 4.dp),
                )
            }

            if (modules.isEmpty() && !loading) {
                EmptyModuleState(onInstall = { zipPicker.launch(arrayOf("application/zip", "application/octet-stream")) })
            } else if (filtered.isEmpty()) {
                Box(
                    Modifier
                        .fillMaxSize()
                        .padding(32.dp),
                    contentAlignment = Alignment.Center,
                ) {
                    Text(
                        "无匹配模块",
                        style = MaterialTheme.typography.bodyMedium,
                        color = MaterialTheme.colorScheme.onSurfaceVariant,
                    )
                }
            } else {
                LazyColumn(
                    modifier = Modifier.fillMaxSize(),
                    contentPadding = androidx.compose.foundation.layout.PaddingValues(
                        start = 16.dp, end = 16.dp, top = 8.dp, bottom = 96.dp,
                    ),
                    verticalArrangement = Arrangement.spacedBy(12.dp),
                ) {
                    items(filtered, key = { it.id }) { module ->
                        ModuleRow(
                            module = module,
                            busy = module.id in busyIds,
                            onToggle = { viewModel.toggleEnabled(module) },
                            onClick = { onOpenDetail(module.id) },
                        )
                    }
                }
            }
        }

        ExtendedFloatingActionButton(
            onClick = { zipPicker.launch(arrayOf("application/zip", "application/octet-stream")) },
            icon = { Icon(Icons.Outlined.Add, contentDescription = null) },
            text = { Text("安装模块") },
            modifier = Modifier
                .align(Alignment.BottomEnd)
                .padding(20.dp),
            containerColor = MaterialTheme.colorScheme.primaryContainer,
            contentColor = MaterialTheme.colorScheme.onPrimaryContainer,
        )
    }
}

@Composable
private fun ModuleRow(
    module: ModuleUi,
    busy: Boolean,
    onToggle: () -> Unit,
    onClick: () -> Unit,
) {
    GlassCard(
        modifier = Modifier.fillMaxWidth(),
        contentPadding = 0.dp,
    ) {
        Row(
            Modifier
                .fillMaxWidth()
                .clickable(onClick = onClick)
                .padding(16.dp),
            verticalAlignment = Alignment.CenterVertically,
        ) {
            Box(
                Modifier
                    .size(40.dp)
                    .clip(CircleShape),
                contentAlignment = Alignment.Center,
            ) {
                Icon(
                    Icons.Outlined.Extension,
                    contentDescription = null,
                    tint = if (module.enabled)
                        MaterialTheme.colorScheme.primary
                    else MaterialTheme.colorScheme.outline,
                )
            }
            Spacer(Modifier.width(12.dp))
            Column(Modifier.weight(1f)) {
                Row(verticalAlignment = Alignment.CenterVertically) {
                    Text(
                        module.name,
                        style = MaterialTheme.typography.titleSmall,
                        fontWeight = FontWeight.SemiBold,
                        maxLines = 1,
                        overflow = TextOverflow.Ellipsis,
                        modifier = Modifier.weight(1f, fill = false),
                    )
                    if (module.hasUpdate) {
                        Spacer(Modifier.width(6.dp))
                        StatusChip(text = "有更新", tone = StatusTone.INFO)
                    }
                }
                Text(
                    "v${module.version} · ${module.author.ifEmpty { "未知作者" }}",
                    style = MaterialTheme.typography.bodySmall,
                    color = MaterialTheme.colorScheme.onSurfaceVariant,
                    maxLines = 1,
                    overflow = TextOverflow.Ellipsis,
                )
                if (module.description.isNotEmpty()) {
                    Text(
                        module.description,
                        style = MaterialTheme.typography.bodySmall,
                        color = MaterialTheme.colorScheme.onSurfaceVariant,
                        maxLines = 2,
                        overflow = TextOverflow.Ellipsis,
                    )
                }
            }
            Spacer(Modifier.width(8.dp))
            Switch(
                checked = module.enabled,
                onCheckedChange = { onToggle() },
                enabled = !busy,
            )
            IconButton(onClick = onClick) {
                Icon(
                    Icons.Outlined.ChevronRight,
                    contentDescription = "详情",
                    tint = MaterialTheme.colorScheme.onSurfaceVariant,
                )
            }
        }
    }
}

@Composable
private fun EmptyState(onInstall: () -> Unit) {
    GlassCard(
        modifier = Modifier
            .fillMaxWidth()
            .padding(16.dp),
        contentPadding = 24.dp,
    ) {
        Column(
            Modifier.fillMaxWidth(),
            horizontalAlignment = Alignment.CenterHorizontally,
        ) {
            Icon(
                Icons.Outlined.Extension,
                contentDescription = null,
                tint = MaterialTheme.colorScheme.outline,
                modifier = Modifier.size(48.dp),
            )
            Spacer(Modifier.height(12.dp))
            Text("还没有模块", style = MaterialTheme.typography.titleMedium)
            Spacer(Modifier.height(4.dp))
            Text(
                "从本地 ZIP 安装第一个模块",
                style = MaterialTheme.typography.bodyMedium,
                color = MaterialTheme.colorScheme.onSurfaceVariant,
            )
            Spacer(Modifier.height(16.dp))
            TextButton(onClick = onInstall) { Text("选择 ZIP 文件") }
        }
    }
}

@Composable
private fun EmptyModuleState(onInstall: () -> Unit) {
    Box(
        Modifier
            .fillMaxSize()
            .padding(16.dp),
        contentAlignment = Alignment.Center,
    ) {
        EmptyState(onInstall = onInstall)
    }
}
