package com.nexus.manager.viewmodel

import androidx.lifecycle.ViewModel
import androidx.lifecycle.viewModelScope
import com.nexus.manager.data.model.SystemStatus
import com.nexus.manager.data.repo.NexusRepository
import com.nexus.manager.ipc.NexusIpcClient
import com.nexus.manager.ipc.proto.RebootRequest
import kotlinx.coroutines.channels.Channel
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.SharingStarted
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.flow.receiveAsFlow
import kotlinx.coroutines.flow.stateIn
import kotlinx.coroutines.launch

/**
 * 仪表盘 ViewModel
 *
 * 职责：
 * - 拉取并展示系统状态（root / selinux / daemon / 模块数 / 安全模式 / 运行时长）
 * - 暴露 IPC 连接状态
 * - 处理重启菜单（正常/软重启/Recovery/Bootloader/Download）
 * - 处理重启 Daemon / 进入安全模式
 */
class DashboardViewModel(
    private val repo: NexusRepository,
) : ViewModel() {

    private val _status = MutableStateFlow(SystemStatus.EMPTY)
    val status: StateFlow<SystemStatus> = _status.asStateFlow()

    /** IPC 连接状态（驱动 UI 上的"Daemon 离线"提示） */
    val connection: StateFlow<NexusIpcClient.Connection> = repo.connection.stateIn(
        scope = viewModelScope,
        started = SharingStarted.WhileSubscribed(5_000),
        initialValue = NexusIpcClient.Connection.Reconnecting(0),
    )

    private val _refreshing = MutableStateFlow(false)
    val refreshing: StateFlow<Boolean> = _refreshing.asStateFlow()

    private val _messages = Channel<String>(Channel.BUFFERED)
    val messages = _messages.receiveAsFlow()

    init {
        refresh()
    }

    fun refresh() {
        viewModelScope.launch {
            _refreshing.value = true
            try {
                _status.value = repo.getStatus()
            } catch (e: Exception) {
                _messages.trySend("刷新失败：${e.message ?: "未知错误"}")
            } finally {
                _refreshing.value = false
            }
        }
    }

    fun restartDaemon() {
        viewModelScope.launch {
            val ok = runCatching { repo.restartDaemon() }.getOrDefault(false)
            _messages.trySend(if (ok) "Daemon 重启请求已发送" else "Daemon 重启失败")
            if (ok) refresh()
        }
    }

    fun enterSafeMode(timeoutSec: Int = 0) {
        viewModelScope.launch {
            val ok = runCatching { repo.enterSafeMode(timeoutSec) }.getOrDefault(false)
            _messages.trySend(
                if (ok) "已进入安全模式，下次重启前模块不加载"
                else "进入安全模式失败"
            )
            if (ok) refresh()
        }
    }

    fun reboot(mode: RebootRequest.Mode) {
        viewModelScope.launch {
            val ok = runCatching { repo.reboot(mode) }.getOrDefault(false)
            val name = when (mode) {
                RebootRequest.Mode.NORMAL -> "正常重启"
                RebootRequest.Mode.USERSPACE -> "软重启"
                RebootRequest.Mode.RECOVERY -> "Recovery"
                RebootRequest.Mode.BOOTLOADER -> "Bootloader"
                RebootRequest.Mode.DOWNLOAD -> "Download"
                RebootRequest.Mode.UNRECOGNIZED -> "重启"
            }
            _messages.trySend(if (ok) "$name 请求已发送" else "$name 失败")
        }
    }

    /** 运行时长格式化为 "Xd Yh Zm" / "Yh Zm Ws" */
    fun formatUptime(ms: Long): String {
        if (ms <= 0) return "—"
        val totalSec = ms / 1000
        val d = totalSec / 86400
        val h = (totalSec % 86400) / 3600
        val m = (totalSec % 3600) / 60
        val s = totalSec % 60
        return when {
            d > 0 -> "${d}d ${h}h ${m}m"
            h > 0 -> "${h}h ${m}m ${s}s"
            else -> "${m}m ${s}s"
        }
    }

    override fun onCleared() {
        super.onCleared()
        _messages.close()
    }
}
