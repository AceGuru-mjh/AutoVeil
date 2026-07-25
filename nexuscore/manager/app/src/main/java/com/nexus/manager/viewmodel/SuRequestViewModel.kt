package com.nexus.manager.viewmodel

import androidx.lifecycle.ViewModel
import androidx.lifecycle.viewModelScope
import com.nexus.manager.data.model.SuPolicy
import com.nexus.manager.data.repo.NexusRepository
import kotlinx.coroutines.Job
import kotlinx.coroutines.channels.Channel
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.flow.receiveAsFlow
import kotlinx.coroutines.launch

/**
 * 实时 Su 请求 ViewModel
 *
 * 订阅 Daemon 推送的 [com.nexus.manager.ipc.proto.SuRequestEvent]，
 * 当任意应用请求 root 时弹出授权对话框（允许/拒绝/仅一次）。
 *
 * 一次只展示一个请求；队列中后续请求在前一个被处理后立即弹出。
 */
class SuRequestViewModel(
    private val repo: NexusRepository,
) : ViewModel() {

    /** 待处理的请求队列（首元素即当前展示的请求） */
    private val _pending = MutableStateFlow<List<Request>>(emptyList())
    val pending: StateFlow<List<Request>> = _pending.asStateFlow()

    val current: Request? get() = _pending.value.firstOrNull()

    private val _messages = Channel<String>(Channel.BUFFERED)
    val messages = _messages.receiveAsFlow()

    private var subscribeJob: Job? = null

    init { restartSubscription() }

    fun respond(policy: SuPolicy) {
        val req = current ?: return
        viewModelScope.launch {
            val ok = runCatching {
                repo.setSuPolicy(req.packageName, req.uid, policy, timeoutSecFor(policy))
            }.getOrDefault(false)
            _messages.trySend(
                if (ok) "${req.packageName} 已${labelFor(policy)}"
                else "授权失败：${req.packageName}"
            )
            // 从队列中移除已处理的请求
            _pending.value = _pending.value.drop(1)
        }
    }

    /**
     * 整改 B5：原 dismiss() 直接 drop 队列，既不通知 daemon 也不持久化策略，
     * daemon 端会一直等待响应直到自己超时；这期间请求方 app 一直挂起。
     *
     * 改为显式 DENY + 短超时（60s），让 daemon 立即解除请求方阻塞，
     * 同时不污染长期策略表（timeoutSec=60 表示 60s 后此 DENY 自动失效）。
     */
    fun dismiss() {
        val req = current ?: return
        viewModelScope.launch {
            runCatching {
                repo.setSuPolicy(req.packageName, req.uid, SuPolicy.DENY, timeoutSec = 60)
            }
            _messages.trySend("${req.packageName} 已稍后处理（短期拒绝 60s）")
            _pending.value = _pending.value.drop(1)
        }
    }

    private fun restartSubscription() {
        subscribeJob?.cancel()
        subscribeJob = viewModelScope.launch {
            repo.subscribeSuRequests().collect { ev ->
                val req = Request(
                    packageName = ev.packageName,
                    uid = ev.uid,
                    pid = ev.pid,
                    command = ev.command,
                )
                _pending.value = _pending.value + req
            }
        }
    }

    private fun timeoutSecFor(policy: SuPolicy) = if (policy == SuPolicy.ALLOW_ONCE) 300 else 0

    private fun labelFor(p: SuPolicy) = when (p) {
        SuPolicy.DENY -> "拒绝"
        SuPolicy.ALLOW -> "允许（永久）"
        SuPolicy.ALLOW_ONCE -> "允许（5 分钟）"
    }

    data class Request(
        val packageName: String,
        val uid: Int,
        val pid: Int,
        val command: String,
    )

    override fun onCleared() {
        super.onCleared()
        subscribeJob?.cancel()
        _messages.close()
    }
}
