package com.nexus.manager.data.repo

import com.nexus.manager.data.model.*
import com.nexus.manager.ipc.IpcException
import com.nexus.manager.ipc.NexusIpcClient
import com.nexus.manager.ipc.proto.*
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.flow.*
import kotlinx.coroutines.withContext

class NexusRepository(private val client: NexusIpcClient) {

    val connection = client.connection

    suspend fun ping(): Boolean = withContext(Dispatchers.IO) {
        runCatching {
            val resp = client.request(Request.newBuilder()
                .setPing(PingRequest.newBuilder().setToken("ping").build()).build())
            resp.code == 0
        }.getOrDefault(false)
    }

    suspend fun getStatus(): SystemStatus = withContext(Dispatchers.IO) {
        runCatching {
            val resp = client.request(Request.newBuilder()
                .setGetStatus(GetStatusRequest.getDefaultInstance()).build())
            if (resp.code != 0) return@runCatching SystemStatus.EMPTY
            val s = resp.getStatus
            SystemStatus(
                rootAvailable = s.rootAvailable,
                rootProvider = s.rootProvider.ifEmpty { "—" },
                rootVersion = s.rootVersion.ifEmpty { "—" },
                selinuxEnforcing = s.selinuxEnforcing,
                selinuxDomain = s.selinuxDomain.ifEmpty { "—" },
                daemonRunning = s.daemonRunning,
                daemonPid = s.daemonPid,
                fsInterceptor = s.fsInterceptor.ifEmpty { "—" },
                moduleCount = s.moduleCount,
                safeMode = s.safeMode,
                uptimeMs = s.uptimeMs,
                androidVersion = s.androidVersion.ifEmpty { android.os.Build.VERSION.RELEASE ?: "—" },
                securityPatch = s.securityPatch.ifEmpty { android.os.Build.VERSION.SECURITY_PATCH ?: "—" },
                kernelVersion = s.kernelVersion.ifEmpty { System.getProperty("os.version") ?: "—" },
                arch = s.arch.ifEmpty { android.os.Build.SUPPORTED_ABIS.firstOrNull() ?: "—" },
                daemonVersion = s.daemonVersion.ifEmpty { "—" },
            )
        }.getOrDefault(SystemStatus.EMPTY)
    }

    suspend fun listModules(): List<ModuleUi> = withContext(Dispatchers.IO) {
        runCatching {
            val resp = client.request(Request.newBuilder()
                .setListModules(ListModulesRequest.getDefaultInstance()).build())
            if (resp.code != 0) emptyList()
            else resp.listModules.modulesList.map { m ->
                ModuleUi(
                    id = m.id, name = m.name.ifEmpty { m.id }, version = m.version,
                    author = m.author, description = m.description,
                    enabled = m.enabled, priority = m.priority,
                    capabilities = m.capabilitiesList.toList(),
                    hasUpdate = m.hasUpdate, updateUrl = m.updateUrl,
                )
            }
        }.getOrDefault(emptyList())
    }

    suspend fun enableModule(id: String): Boolean = withContext(Dispatchers.IO) {
        runCatching { sendSimple(setEnableModule(id)) }.isSuccess
    }

    suspend fun disableModule(id: String): Boolean = withContext(Dispatchers.IO) {
        runCatching { sendSimple(setDisableModule(id)) }.isSuccess
    }

    suspend fun installModule(localPath: String): Result<InstallModuleResponse> = withContext(Dispatchers.IO) {
        runCatching {
            val resp = client.request(Request.newBuilder()
                .setInstallModule(InstallModuleRequest.newBuilder()
                    .setLocalPath(localPath).build()).build())
            if (resp.code != 0) throw IpcException.RemoteError(resp.code, resp.message)
            resp.installModule
        }
    }

    suspend fun uninstallModule(id: String): Boolean = withContext(Dispatchers.IO) {
        runCatching { sendSimple(setUninstallModule(id)) }.isSuccess
    }

    suspend fun restartDaemon(): Boolean = withContext(Dispatchers.IO) {
        runCatching { sendSimple(RestartDaemonRequest.getDefaultInstance()) }.isSuccess
    }

    suspend fun enterSafeMode(timeoutSec: Int = 0): Boolean = withContext(Dispatchers.IO) {
        runCatching {
            sendSimple(EnterSafeModeRequest.newBuilder().setTimeoutSec(timeoutSec).build())
        }.isSuccess
    }

    suspend fun reboot(mode: RebootRequest.Mode): Boolean = withContext(Dispatchers.IO) {
        runCatching {
            sendSimple(RebootRequest.newBuilder().setMode(mode).build())
        }.isSuccess
    }

    suspend fun listSuApps(): List<SuAppUi> = withContext(Dispatchers.IO) {
        runCatching {
            val resp = client.request(Request.newBuilder()
                .setListSuApps(ListSuAppsRequest.getDefaultInstance()).build())
            if (resp.code != 0) emptyList()
            else resp.listSuApps.appsList.map {
                SuAppUi(
                    packageName = it.packageName, uid = it.uid,
                    policy = SuPolicy.entries.getOrNull(it.policy.number) ?: SuPolicy.DENY,
                    lastRequestMs = it.lastRequestMs,
                    requestCount = it.requestCount,
                    timeoutSec = it.timeoutSec,
                )
            }
        }.getOrDefault(emptyList())
    }

    suspend fun setSuPolicy(packageName: String, uid: Int, policy: SuPolicy, timeoutSec: Int = 0): Boolean =
        withContext(Dispatchers.IO) {
            runCatching {
                sendSimple(SetSuPolicyRequest.newBuilder()
                    .setPackageName(packageName).setUid(uid)
                    .setPolicy(SetSuPolicyRequest.Policy.forNumber(policy.ordinal))
                    .setTimeoutSec(timeoutSec).build())
            }.isSuccess
        }

    suspend fun listSuLogs(): List<SuLogEntryUi> = withContext(Dispatchers.IO) {
        runCatching {
            val resp = client.request(Request.newBuilder()
                .setListSuLogs(ListSuLogsRequest.getDefaultInstance()).build())
            if (resp.code != 0) emptyList()
            else resp.listSuLogs.entriesList.map {
                SuLogEntryUi(it.timestampMs, it.packageName, it.uid, it.granted, it.command)
            }
        }.getOrDefault(emptyList())
    }

    suspend fun clearLogs(target: ClearLogsRequest.Target): Boolean = withContext(Dispatchers.IO) {
        runCatching {
            sendSimple(ClearLogsRequest.newBuilder().setTarget(target).build())
        }.isSuccess
    }

    suspend fun uninstallFramework(): Boolean = withContext(Dispatchers.IO) {
        runCatching { sendSimple(UninstallFrameworkRequest.getDefaultInstance()) }.isSuccess
    }

    /** 订阅 Daemon 日志流 */
    fun subscribeLogs(minLevel: Int): Flow<LogLine> = client.events
        .filter { it.payloadCase == Event.PayloadCase.LOG_LINE }
        .map { e ->
            val l = e.logLine
            LogLine(l.level, l.tag, l.msg, e.timestampMs)
        }
        .filter { it.level >= minLevel }

    /** 订阅 SuRequest 事件 */
    fun subscribeSuRequests(): Flow<SuRequestEvent> = client.events
        .filter { it.payloadCase == Event.PayloadCase.SU_REQUEST }
        .map { it.suRequest }

    private fun setEnableModule(id: String) =
        EnableModuleRequest.newBuilder().setId(id).build()
    private fun setDisableModule(id: String) =
        DisableModuleRequest.newBuilder().setId(id).build()
    private fun setUninstallModule(id: String) =
        UninstallModuleRequest.newBuilder().setId(id).build()

    private suspend fun sendSimple(req: com.google.protobuf.MessageLite) {
        val builder = Request.newBuilder()
        when (req) {
            is EnableModuleRequest         -> builder.setEnableModule(req)
            is DisableModuleRequest        -> builder.setDisableModule(req)
            is UninstallModuleRequest      -> builder.setUninstallModule(req)
            is RestartDaemonRequest        -> builder.setRestartDaemon(req)
            is EnterSafeModeRequest        -> builder.setEnterSafeMode(req)
            is RebootRequest               -> builder.setReboot(req)
            is SetSuPolicyRequest          -> builder.setSetSuPolicy(req)
            is ClearLogsRequest            -> builder.setClearLogs(req)
            is UninstallFrameworkRequest   -> builder.setUninstallFramework(req)
            else -> throw IllegalArgumentException("unsupported request type")
        }
        val resp = client.request(builder.build())
        if (resp.code != 0) throw IpcException.RemoteError(resp.code, resp.message)
    }
}
