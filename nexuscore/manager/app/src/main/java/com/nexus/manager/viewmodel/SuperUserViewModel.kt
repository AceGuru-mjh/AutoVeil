package com.nexus.manager.viewmodel

import androidx.lifecycle.ViewModel
import androidx.lifecycle.viewModelScope
import com.nexus.manager.data.model.SuAppUi
import com.nexus.manager.data.model.SuLogEntryUi
import com.nexus.manager.data.model.SuPolicy
import com.nexus.manager.data.repo.NexusRepository
import com.nexus.manager.ipc.proto.ClearLogsRequest
import kotlinx.coroutines.channels.Channel
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.flow.receiveAsFlow
import kotlinx.coroutines.launch

/**
 * 超级用户 ViewModel
 *
 * 职责：
 * - 列出已授权 root 的应用
 * - 修改单个应用的授权策略（拒绝/允许/仅一次）
 * - 列出 root 调用日志
 * - 清除 root 日志
 */
class SuperUserViewModel(
    private val repo: NexusRepository,
) : ViewModel() {

    private val _apps = MutableStateFlow<List<SuAppUi>>(emptyList())
    val apps: StateFlow<List<SuAppUi>> = _apps.asStateFlow()

    private val _logs = MutableStateFlow<List<SuLogEntryUi>>(emptyList())
    val logs: StateFlow<List<SuLogEntryUi>> = _logs.asStateFlow()

    private val _loading = MutableStateFlow(false)
    val loading: StateFlow<Boolean> = _loading.asStateFlow()

    private val _messages = Channel<String>(Channel.BUFFERED)
    val messages = _messages.receiveAsFlow()

    init { refresh() }

    fun refresh() {
        viewModelScope.launch {
            _loading.value = true
            try {
                _apps.value = repo.listSuApps()
                _logs.value = repo.listSuLogs()
            } catch (e: Exception) {
                _messages.trySend("刷新失败：${e.message ?: "未知错误"}")
            } finally {
                _loading.value = false
            }
        }
    }

    fun setPolicy(app: SuAppUi, policy: SuPolicy, timeoutSec: Int = 0) {
        viewModelScope.launch {
            val ok = runCatching {
                repo.setSuPolicy(app.packageName, app.uid, policy, timeoutSec)
            }.getOrDefault(false)
            val name = when (policy) {
                SuPolicy.DENY -> "拒绝"
                SuPolicy.ALLOW -> "允许"
                SuPolicy.ALLOW_ONCE -> "仅一次"
            }
            _messages.trySend(if (ok) "已设为$name" else "设置失败")
            if (ok) _apps.value = _apps.value.map {
                if (it.packageName == app.packageName && it.uid == app.uid) {
                    it.copy(policy = policy, timeoutSec = timeoutSec)
                } else it
            }
        }
    }

    fun clearLogs() {
        viewModelScope.launch {
            val ok = runCatching {
                repo.clearLogs(ClearLogsRequest.Target.SU)
            }.getOrDefault(false)
            _messages.trySend(if (ok) "日志已清除" else "清除失败")
            if (ok) _logs.value = emptyList()
        }
    }

    override fun onCleared() {
        super.onCleared()
        _messages.close()
    }
}
