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

import androidx.lifecycle.ViewModel
import androidx.lifecycle.viewModelScope
import com.nexus.manager.data.model.LogLine
import com.nexus.manager.data.repo.NexusRepository
import com.nexus.manager.ipc.proto.ClearLogsRequest
import kotlinx.coroutines.Job
import kotlinx.coroutines.channels.Channel
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.flow.receiveAsFlow
import kotlinx.coroutines.launch

/**
 * 日志 ViewModel
 *
 * 职责：
 * - 订阅 Daemon 实时日志流（按最低级别过滤）
 * - 维护滚动日志缓冲（最多 [MAX_BUFFER] 行，超出裁掉旧行）
 * - 暂停/恢复订阅
 * - 清除日志（Daemon 端 + 本地缓冲）
 * - 导出日志到 Download 目录（业务在 UI 侧用 FileBridge 落盘）
 *
 * 注意：日志缓冲放内存，避免无界增长。导出时直接把当前缓冲写入文件。
 */
class LogsViewModel(
    private val repo: NexusRepository,
) : ViewModel() {

    private val _lines = MutableStateFlow<List<LogLine>>(emptyList())
    val lines: StateFlow<List<LogLine>> = _lines.asStateFlow()

    private val _minLevel = MutableStateFlow(2) // Info 默认
    val minLevel: StateFlow<Int> = _minLevel.asStateFlow()

    private val _paused = MutableStateFlow(false)
    val paused: StateFlow<Boolean> = _paused.asStateFlow()

    private val _messages = Channel<String>(Channel.BUFFERED)
    val messages = _messages.receiveAsFlow()

    private var subscribeJob: Job? = null

    init { restartSubscription() }

    fun setMinLevel(level: Int) {
        _minLevel.value = level.coerceIn(0, 4)
        restartSubscription()
    }

    fun togglePause() {
        _paused.value = !_paused.value
        if (_paused.value) {
            subscribeJob?.cancel()
            subscribeJob = null
        } else {
            restartSubscription()
        }
    }

    fun clearLocal() {
        _lines.value = emptyList()
    }

    fun clearRemote() {
        viewModelScope.launch {
            val ok = runCatching {
                repo.clearLogs(ClearLogsRequest.Target.DAEMON)
            }.getOrDefault(false)
            _messages.trySend(if (ok) "Daemon 日志已清除" else "清除失败")
            if (ok) clearLocal()
        }
    }

    /** 导出快照：返回当前缓冲的可写文本 */
    fun snapshotText(): String {
        val sb = StringBuilder(1024 * (_lines.value.size.coerceAtMost(2048)))
        sb.append("# NexusCore daemon log snapshot\n")
        sb.append("# generated: ${System.currentTimeMillis()}\n")
        sb.append("# level >= ${_minLevel.value} (${levelName(_minLevel.value)})\n\n")
        _lines.value.forEach { line ->
            sb.append('[').append(levelName(line.level)).append("] ")
            sb.append(line.tag).append(": ").append(line.msg).append('\n')
        }
        return sb.toString()
    }

    private fun restartSubscription() {
        subscribeJob?.cancel()
        if (_paused.value) return
        subscribeJob = viewModelScope.launch {
            repo.subscribeLogs(_minLevel.value).collect { line ->
                val current = _lines.value
                val next = if (current.size >= MAX_BUFFER) {
                    current.drop(current.size - MAX_BUFFER + 1) + line
                } else {
                    current + line
                }
                _lines.value = next
            }
        }
    }

    private fun levelName(level: Int) = when (level) {
        0 -> "V"; 1 -> "D"; 2 -> "I"; 3 -> "W"; 4 -> "E"; else -> "?"
    }

    override fun onCleared() {
        super.onCleared()
        subscribeJob?.cancel()
        _messages.close()
    }

    companion object {
        private const val MAX_BUFFER = 2000
    }
}
