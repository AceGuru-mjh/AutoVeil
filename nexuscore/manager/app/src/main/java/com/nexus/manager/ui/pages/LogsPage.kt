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
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.background
import androidx.compose.foundation.horizontalScroll
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.lazy.rememberLazyListState
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.outlined.Close
import androidx.compose.material.icons.outlined.DeleteOutline
import androidx.compose.material.icons.outlined.IosShare
import androidx.compose.material.icons.outlined.Pause
import androidx.compose.material.icons.outlined.PlayArrow
import androidx.compose.material.icons.outlined.Search
import androidx.compose.material.icons.outlined.Share
import androidx.compose.material3.FilterChip
import androidx.compose.material3.FilterChipDefaults
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.unit.dp
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import androidx.lifecycle.viewmodel.compose.viewModel
import com.nexus.manager.data.bridge.FileBridge
import com.nexus.manager.data.model.LogLine
import com.nexus.manager.ui.components.GlassTopBar
import com.nexus.manager.ui.components.CollectMessages
import com.nexus.manager.ui.components.StatusTone
import com.nexus.manager.ui.components.color
import com.nexus.manager.viewmodel.LogsViewModel
import com.nexus.manager.viewmodel.NexusViewModelFactory
import java.text.SimpleDateFormat
import java.util.Date
import java.util.Locale

@Composable
fun LogsPage(
    viewModel: LogsViewModel = viewModel(factory = NexusViewModelFactory()),
) {
    val lines by viewModel.lines.collectAsStateWithLifecycle()
    val minLevel by viewModel.minLevel.collectAsStateWithLifecycle()
    val paused by viewModel.paused.collectAsStateWithLifecycle()
    val context = LocalContext.current
    val listState = rememberLazyListState()
    var query by remember { mutableStateOf("") }
    var searchOpen by remember { mutableStateOf(false) }

    CollectMessages(viewModel.messages)

    // 过滤后日志：按级别（VM 已订阅时过滤）+ 关键字（tag/msg 包含）
    val filtered = remember(lines, query) {
        if (query.isBlank()) lines
        else lines.filter {
            it.tag.contains(query, ignoreCase = true) ||
                it.msg.contains(query, ignoreCase = true)
        }
    }

    val exportLauncher = rememberLauncherForActivityResult(
        contract = ActivityResultContracts.CreateDocument("text/plain"),
    ) { uri ->
        if (uri != null) {
            val text = viewModel.snapshotText()
            val ok = FileBridge.writeTextToUri(context, uri, text)
            android.widget.Toast.makeText(
                context,
                if (ok) "日志已导出" else "导出失败",
                android.widget.Toast.LENGTH_SHORT,
            ).show()
        }
    }

    // 新日志到达时自动滚动到底部（仅未暂停且无搜索关键字时）
    LaunchedEffect(filtered.size) {
        if (filtered.isNotEmpty() && !paused && query.isBlank()) {
            listState.animateScrollToItem(filtered.size - 1)
        }
    }

    Column(Modifier.fillMaxSize()) {
        GlassTopBar(
            title = "日志",
            subtitle = if (paused) "已暂停" else "实时流",
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
                IconButton(onClick = viewModel::togglePause) {
                    Icon(
                        if (paused) Icons.Outlined.PlayArrow else Icons.Outlined.Pause,
                        contentDescription = if (paused) "继续" else "暂停",
                    )
                }
                IconButton(onClick = {
                    // 分享当前快照到其他 App（聊天/笔记等）
                    val text = viewModel.snapshotText()
                    val send = Intent(Intent.ACTION_SEND).apply {
                        type = "text/plain"
                        putExtra(Intent.EXTRA_SUBJECT, "NexusCore log ${System.currentTimeMillis()}")
                        putExtra(Intent.EXTRA_TEXT, text)
                    }
                    runCatching {
                        context.startActivity(Intent.createChooser(send, "分享日志").apply {
                            addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)
                        })
                    }
                }) {
                    Icon(Icons.Outlined.Share, contentDescription = "分享")
                }
                IconButton(onClick = { exportLauncher.launch("nexuscore_log_${System.currentTimeMillis()}.txt") }) {
                    Icon(Icons.Outlined.IosShare, contentDescription = "导出")
                }
                IconButton(onClick = viewModel::clearRemote) {
                    Icon(Icons.Outlined.DeleteOutline, contentDescription = "清除")
                }
            },
        )

        // 搜索框
        if (searchOpen) {
            OutlinedTextField(
                value = query,
                onValueChange = { query = it },
                modifier = Modifier
                    .fillMaxWidth()
                    .padding(horizontal = 16.dp, vertical = 4.dp),
                placeholder = { Text("搜索 tag 或内容…") },
                leadingIcon = { Icon(Icons.Outlined.Search, contentDescription = null) },
                singleLine = true,
                shape = RoundedCornerShape(16.dp),
            )
        }

        // 级别过滤
        Row(
            Modifier
                .fillMaxWidth()
                .horizontalScroll(rememberScrollState())
                .padding(horizontal = 16.dp, vertical = 8.dp),
            horizontalArrangement = Arrangement.spacedBy(8.dp),
        ) {
            levelOptions.forEach { (level, label) ->
                FilterChip(
                    selected = minLevel == level,
                    onClick = { viewModel.setMinLevel(level) },
                    label = { Text(label) },
                    colors = FilterChipDefaults.filterChipColors(
                        selectedContainerColor = MaterialTheme.colorScheme.primaryContainer,
                        selectedLabelColor = MaterialTheme.colorScheme.onPrimaryContainer,
                    ),
                )
            }
        }

        if (filtered.isEmpty()) {
            Box(
                Modifier
                    .fillMaxSize()
                    .padding(32.dp),
                contentAlignment = Alignment.Center,
            ) {
                Text(
                    if (query.isNotBlank()) "无匹配日志"
                    else if (paused) "日志流已暂停"
                    else "等待日志…",
                    style = MaterialTheme.typography.bodyMedium,
                    color = MaterialTheme.colorScheme.onSurfaceVariant,
                )
            }
        } else {
            LazyColumn(
                state = listState,
                modifier = Modifier.fillMaxSize(),
                contentPadding = androidx.compose.foundation.layout.PaddingValues(
                    start = 16.dp, end = 16.dp, bottom = 96.dp,
                ),
                verticalArrangement = Arrangement.spacedBy(2.dp),
            ) {
                items(filtered, key = { it.timestampMs.toString() + it.msg.hashCode() + it.tag }) { line ->
                    LogLineRow(line, highlight = query.isNotBlank() &&
                        (line.tag.contains(query, ignoreCase = true) ||
                            line.msg.contains(query, ignoreCase = true)))
                }
            }
        }
    }
}

@Composable
private fun LogLineRow(line: LogLine, highlight: Boolean = false) {
    val df = remember { SimpleDateFormat("HH:mm:ss.SSS", Locale.getDefault()) }
    val tone = when (line.level) {
        0, 1 -> StatusTone.NEUTRAL
        2 -> StatusTone.INFO
        3 -> StatusTone.WARN
        4 -> StatusTone.ERROR
        else -> StatusTone.NEUTRAL
    }
    val color = tone.color()
    Row(
        Modifier
            .fillMaxWidth()
            .clip(RoundedCornerShape(8.dp))
            .background(color.copy(alpha = if (highlight) 0.18f else 0.06f))
            .padding(horizontal = 8.dp, vertical = 4.dp),
    ) {
        Text(
            text = "${line.levelName} ${df.format(Date(line.timestampMs))}",
            style = MaterialTheme.typography.labelSmall,
            color = color,
            fontFamily = FontFamily.Monospace,
            modifier = Modifier.width(140.dp),
        )
        Text(
            text = line.tag,
            style = MaterialTheme.typography.labelSmall,
            color = MaterialTheme.colorScheme.primary,
            fontFamily = FontFamily.Monospace,
            modifier = Modifier.width(80.dp),
            maxLines = 1,
        )
        Text(
            text = line.msg,
            style = MaterialTheme.typography.bodySmall,
            color = MaterialTheme.colorScheme.onSurface,
            fontFamily = FontFamily.Monospace,
        )
    }
}

private val levelOptions = listOf(
    0 to "Verbose",
    1 to "Debug",
    2 to "Info",
    3 to "Warn",
    4 to "Error",
)
