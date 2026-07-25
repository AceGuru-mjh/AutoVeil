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

import androidx.compose.foundation.background
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
import androidx.compose.material.icons.outlined.Close
import androidx.compose.material.icons.outlined.DeleteOutline
import androidx.compose.material.icons.outlined.Person
import androidx.compose.material.icons.outlined.Refresh
import androidx.compose.material.icons.outlined.Search
import androidx.compose.material.icons.outlined.Security
import androidx.compose.material3.CircularProgressIndicator
import androidx.compose.material3.DropdownMenu
import androidx.compose.material3.DropdownMenuItem
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.PrimaryTabRow
import androidx.compose.material3.Tab
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import androidx.lifecycle.viewmodel.compose.viewModel
import com.nexus.manager.data.model.SuAppUi
import com.nexus.manager.data.model.SuLogEntryUi
import com.nexus.manager.data.model.SuPolicy
import com.nexus.manager.ui.components.GlassCard
import com.nexus.manager.ui.components.GlassTopBar
import com.nexus.manager.ui.components.CollectMessages
import com.nexus.manager.ui.components.SectionHeader
import com.nexus.manager.ui.components.StatusChip
import com.nexus.manager.ui.components.StatusTone
import com.nexus.manager.ui.components.color
import com.nexus.manager.viewmodel.NexusViewModelFactory
import com.nexus.manager.viewmodel.SuperUserViewModel
import java.text.SimpleDateFormat
import java.util.Date
import java.util.Locale

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun SuperUserPage(
    viewModel: SuperUserViewModel = viewModel(factory = NexusViewModelFactory()),
) {
    val apps by viewModel.apps.collectAsStateWithLifecycle()
    val logs by viewModel.logs.collectAsStateWithLifecycle()
    val loading by viewModel.loading.collectAsStateWithLifecycle()
    var tab by remember { mutableStateOf(0) }
    var query by remember { mutableStateOf("") }
    var searchOpen by remember { mutableStateOf(false) }

    CollectMessages(viewModel.messages)

    val filteredApps = remember(apps, query) {
        if (query.isBlank()) apps
        else apps.filter {
            it.packageName.contains(query, ignoreCase = true) ||
                "${it.uid}".contains(query)
        }
    }

    Column(Modifier.fillMaxSize()) {
        GlassTopBar(
            title = "超级用户",
            subtitle = "Root 授权与日志",
            actions = {
                // 仅应用 tab 显示搜索
                if (tab == 0) {
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

        if (searchOpen && tab == 0) {
            OutlinedTextField(
                value = query,
                onValueChange = { query = it },
                modifier = Modifier
                    .fillMaxWidth()
                    .padding(horizontal = 16.dp, vertical = 4.dp),
                placeholder = { Text("搜索包名 / UID…") },
                leadingIcon = { Icon(Icons.Outlined.Search, contentDescription = null) },
                singleLine = true,
                shape = RoundedCornerShape(16.dp),
            )
        }

        PrimaryTabRow(
            selectedTabIndex = tab,
            containerColor = androidx.compose.ui.graphics.Color.Transparent,
        ) {
            Tab(selected = tab == 0, onClick = { tab = 0 }, text = { Text("应用 (${filteredApps.size})") })
            Tab(selected = tab == 1, onClick = { tab = 1 }, text = { Text("日志 (${logs.size})") })
        }

        when (tab) {
            0 -> AppsTab(apps = filteredApps, onSetPolicy = viewModel::setPolicy)
            1 -> LogsTab(logs = logs, onClear = viewModel::clearLogs)
        }
    }
}

@Composable
private fun AppsTab(
    apps: List<SuAppUi>,
    onSetPolicy: (SuAppUi, SuPolicy, Int) -> Unit,
) {
    if (apps.isEmpty()) {
        EmptyHint(icon = Icons.Outlined.Security, text = "暂无应用请求过 Root 权限")
        return
    }
    LazyColumn(
        modifier = Modifier.fillMaxSize(),
        contentPadding = androidx.compose.foundation.layout.PaddingValues(16.dp),
        verticalArrangement = Arrangement.spacedBy(12.dp),
    ) {
        items(apps, key = { "${it.packageName}:${it.uid}" }) { app ->
            SuAppCard(app = app, onSetPolicy = { p, t -> onSetPolicy(app, p, t) })
        }
    }
}

@Composable
private fun SuAppCard(
    app: SuAppUi,
    onSetPolicy: (SuPolicy, Int) -> Unit,
) {
    var menuOpen by remember { mutableStateOf(false) }
    GlassCard(modifier = Modifier.fillMaxWidth(), contentPadding = 0.dp) {
        Row(
            Modifier
                .fillMaxWidth()
                .padding(16.dp),
            verticalAlignment = Alignment.CenterVertically,
        ) {
            Box(
                Modifier
                    .size(40.dp)
                    .clip(CircleShape),
                contentAlignment = Alignment.Center,
            ) {
                Icon(Icons.Outlined.Person, contentDescription = null, tint = MaterialTheme.colorScheme.primary)
            }
            Spacer(Modifier.width(12.dp))
            Column(Modifier.weight(1f)) {
                Text(
                    app.packageName,
                    style = MaterialTheme.typography.titleSmall,
                    fontWeight = FontWeight.SemiBold,
                    maxLines = 1,
                    overflow = TextOverflow.Ellipsis,
                )
                Text(
                    "uid=${app.uid} · 请求 ${app.requestCount} 次",
                    style = MaterialTheme.typography.bodySmall,
                    color = MaterialTheme.colorScheme.onSurfaceVariant,
                )
            }
            Spacer(Modifier.width(8.dp))
            Box {
                PolicyChip(policy = app.policy, onClick = { menuOpen = true })
                DropdownMenu(expanded = menuOpen, onDismissRequest = { menuOpen = false }) {
                    SuPolicy.entries.forEach { p ->
                        DropdownMenuItem(
                            text = { Text(policyLabel(p)) },
                            onClick = {
                                onSetPolicy(p, if (p == SuPolicy.ALLOW_ONCE) 300 else 0)
                                menuOpen = false
                            },
                        )
                    }
                }
            }
        }
    }
}

@Composable
private fun PolicyChip(policy: SuPolicy, onClick: () -> Unit) {
    val (text, tone) = when (policy) {
        SuPolicy.DENY -> "拒绝" to StatusTone.ERROR
        SuPolicy.ALLOW -> "允许" to StatusTone.OK
        SuPolicy.ALLOW_ONCE -> "仅一次" to StatusTone.WARN
    }
    val color = tone.color()
    Row(
        modifier = Modifier
            .clip(RoundedCornerShape(50))
            .clickable(onClick = onClick)
            .background(color.copy(alpha = 0.15f))
            .padding(horizontal = 12.dp, vertical = 6.dp),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        Box(
            Modifier
                .size(6.dp)
                .clip(CircleShape)
                .background(color),
        )
        Spacer(Modifier.width(6.dp))
        Text(
            text = text,
            style = MaterialTheme.typography.labelMedium,
            color = color,
            fontWeight = FontWeight.SemiBold,
        )
    }
}

private fun policyLabel(p: SuPolicy) = when (p) {
    SuPolicy.DENY -> "拒绝"
    SuPolicy.ALLOW -> "允许（永久）"
    SuPolicy.ALLOW_ONCE -> "仅一次（5 分钟）"
}

@Composable
private fun LogsTab(
    logs: List<SuLogEntryUi>,
    onClear: () -> Unit,
) {
    Column(Modifier.fillMaxSize()) {
        Row(
            Modifier
                .fillMaxWidth()
                .padding(horizontal = 16.dp, vertical = 4.dp),
            verticalAlignment = Alignment.CenterVertically,
        ) {
            SectionHeader(
                title = "最近调用",
                modifier = Modifier.weight(1f),
                trailing = {
                    IconButton(onClick = onClear) {
                        Icon(Icons.Outlined.DeleteOutline, contentDescription = "清除日志")
                    }
                },
            )
        }
        if (logs.isEmpty()) {
            EmptyHint(icon = Icons.Outlined.Security, text = "暂无 Root 调用记录")
            return@Column
        }
        LazyColumn(
            modifier = Modifier.fillMaxSize(),
            contentPadding = androidx.compose.foundation.layout.PaddingValues(
                start = 16.dp, end = 16.dp, bottom = 96.dp,
            ),
            verticalArrangement = Arrangement.spacedBy(8.dp),
        ) {
            items(logs, key = { it.timestampMs.toString() + it.packageName }) { entry ->
                SuLogRow(entry)
            }
        }
    }
}

@Composable
private fun SuLogRow(entry: SuLogEntryUi) {
    val df = remember { SimpleDateFormat("MM-dd HH:mm:ss", Locale.getDefault()) }
    GlassCard(modifier = Modifier.fillMaxWidth(), contentPadding = 12.dp) {
        Column {
            Row(verticalAlignment = Alignment.CenterVertically) {
                Text(
                    entry.packageName,
                    style = MaterialTheme.typography.titleSmall,
                    fontWeight = FontWeight.Medium,
                    modifier = Modifier.weight(1f),
                    maxLines = 1,
                    overflow = TextOverflow.Ellipsis,
                )
                StatusChip(
                    text = if (entry.granted) "已授权" else "已拒绝",
                    tone = if (entry.granted) StatusTone.OK else StatusTone.ERROR,
                )
            }
            Spacer(Modifier.height(4.dp))
            Text(
                entry.command,
                style = MaterialTheme.typography.bodySmall,
                color = MaterialTheme.colorScheme.onSurfaceVariant,
                maxLines = 2,
                overflow = TextOverflow.Ellipsis,
            )
            Text(
                "${df.format(Date(entry.timestampMs))} · uid=${entry.uid}",
                style = MaterialTheme.typography.labelSmall,
                color = MaterialTheme.colorScheme.outline,
            )
        }
    }
}

@Composable
private fun EmptyHint(icon: androidx.compose.ui.graphics.vector.ImageVector, text: String) {
    Box(
        Modifier
            .fillMaxSize()
            .padding(32.dp),
        contentAlignment = Alignment.Center,
    ) {
        Column(horizontalAlignment = Alignment.CenterHorizontally) {
            Icon(icon, contentDescription = null, tint = MaterialTheme.colorScheme.outline, modifier = Modifier.size(40.dp))
            Spacer(Modifier.height(8.dp))
            Text(text, style = MaterialTheme.typography.bodyMedium, color = MaterialTheme.colorScheme.onSurfaceVariant)
        }
    }
}
