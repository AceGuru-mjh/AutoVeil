package com.nexus.manager.viewmodel

import androidx.lifecycle.ViewModel
import androidx.lifecycle.viewModelScope
import com.nexus.manager.data.model.ModuleUi
import com.nexus.manager.data.repo.NexusRepository
import kotlinx.coroutines.channels.Channel
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.SharingStarted
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.flow.receiveAsFlow
import kotlinx.coroutines.flow.stateIn
import kotlinx.coroutines.launch

/**
 * 模块管理 ViewModel
 *
 * 职责：
 * - 列出已安装模块
 * - 启用/禁用/卸载单个模块
 * - 从本地 ZIP 安装新模块（路径由 UI 通过 SAF 选取后传入）
 * - 跟踪安装/卸载中的模块 id，驱动 UI loading 态
 */
class ModulesViewModel(
    private val repo: NexusRepository,
) : ViewModel() {

    private val _modules = MutableStateFlow<List<ModuleUi>>(emptyList())
    val modules: StateFlow<List<ModuleUi>> = _modules.asStateFlow()

    private val _loading = MutableStateFlow(false)
    val loading: StateFlow<Boolean> = _loading.asStateFlow()

    /** 正在操作（启用/禁用/卸载）的模块 id 集合 */
    private val _busyIds = MutableStateFlow<Set<String>>(emptySet())
    val busyIds: StateFlow<Set<String>> = _busyIds.asStateFlow()

    /** 正在安装的本地路径（用于顶部进度条） */
    private val _installing = MutableStateFlow(false)
    val installing: StateFlow<Boolean> = _installing.asStateFlow()

    private val _messages = Channel<String>(Channel.BUFFERED)
    val messages = _messages.receiveAsFlow()

    private val _lastInstall = MutableStateFlow<InstallResult?>(null)
    val lastInstall: StateFlow<InstallResult?> = _lastInstall.asStateFlow()

    init { refresh() }

    fun refresh() {
        viewModelScope.launch {
            _loading.value = true
            try {
                _modules.value = repo.listModules()
            } catch (e: Exception) {
                _messages.trySend("加载模块列表失败：${e.message ?: "未知错误"}")
            } finally {
                _loading.value = false
            }
        }
    }

    fun toggleEnabled(module: ModuleUi) {
        if (module.id in _busyIds.value) return
        viewModelScope.launch {
            markBusy(module.id, true)
            val ok = runCatching {
                if (module.enabled) repo.disableModule(module.id)
                else repo.enableModule(module.id)
            }.getOrDefault(false)
            _messages.trySend(
                if (ok) "${module.name} 已${if (module.enabled) "禁用" else "启用"}"
                else "操作失败"
            )
            markBusy(module.id, false)
            if (ok) refresh()
        }
    }

    fun uninstall(module: ModuleUi) {
        if (module.id in _busyIds.value) return
        viewModelScope.launch {
            markBusy(module.id, true)
            val ok = runCatching { repo.uninstallModule(module.id) }.getOrDefault(false)
            _messages.trySend(if (ok) "${module.name} 已卸载" else "卸载失败")
            markBusy(module.id, false)
            if (ok) refresh()
        }
    }

    /**
     * 安装本地 ZIP。
     *
     * 整改 B12：原代码用 runCatching 包裹一个已返回 Result 的调用，外层 getOrElse
     * 永远不会触发（除非 _lastInstall.value = 赋值抛异常），意图混乱。
     * 改为直接 try/catch，错误链清晰。
     *
     * @param localPath 已复制到 Daemon 可访问目录的 ZIP 绝对路径
     */
    fun installFromZip(localPath: String) {
        if (_installing.value) return
        viewModelScope.launch {
            _installing.value = true
            val result = try {
                val resp = repo.installModule(localPath)
                if (resp.isSuccess) {
                    val r = resp.getOrThrow()
                    InstallResult(success = true, id = r.id, needReboot = r.needReboot)
                } else {
                    InstallResult(success = false, error = resp.exceptionOrNull()?.message ?: "安装失败")
                }
            } catch (e: Exception) {
                InstallResult(success = false, error = e.message ?: "安装失败")
            }
            _lastInstall.value = result
            _messages.trySend(
                if (result.success) "安装成功${if (result.needReboot) "，重启后生效" else ""}"
                else "安装失败：${result.error}"
            )
            _installing.value = false
            if (result.success) refresh()
        }
    }

    fun consumeLastInstall() { _lastInstall.value = null }

    private fun markBusy(id: String, busy: Boolean) {
        _busyIds.value = if (busy) _busyIds.value + id else _busyIds.value - id
    }

    data class InstallResult(
        val success: Boolean,
        val id: String = "",
        val needReboot: Boolean = false,
        val error: String = "",
    )

    override fun onCleared() {
        super.onCleared()
        _messages.close()
    }
}
