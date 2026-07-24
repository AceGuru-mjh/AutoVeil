package com.nexus.manager.data.model

/**
 * UI 层数据模型，与 proto 解耦
 */

data class SystemStatus(
    val rootAvailable: Boolean,
    val rootProvider: String,
    val rootVersion: String,
    val selinuxEnforcing: Boolean,
    val selinuxDomain: String,
    val daemonRunning: Boolean,
    val daemonPid: Int,
    val fsInterceptor: String,
    val moduleCount: Int,
    val safeMode: Boolean,
    val uptimeMs: Long,
    val androidVersion: String,
    val securityPatch: String,
    val kernelVersion: String,
    val arch: String,
    val daemonVersion: String,
) {
    companion object {
        val EMPTY = SystemStatus(
            rootAvailable = false, rootProvider = "—", rootVersion = "—",
            selinuxEnforcing = false, selinuxDomain = "—",
            daemonRunning = false, daemonPid = 0,
            fsInterceptor = "—", moduleCount = 0, safeMode = false, uptimeMs = 0,
            androidVersion = "—", securityPatch = "—", kernelVersion = "—",
            arch = "—", daemonVersion = "—"
        )
    }
}

data class ModuleUi(
    val id: String,
    val name: String,
    val version: String,
    val author: String,
    val description: String,
    val enabled: Boolean,
    val priority: Int,
    val capabilities: List<String>,
    val hasUpdate: Boolean = false,
    val updateUrl: String = "",
)

data class SuAppUi(
    val packageName: String,
    val uid: Int,
    val policy: SuPolicy,
    val lastRequestMs: Long,
    val requestCount: Int,
    val timeoutSec: Int,  // 0=永久
)

enum class SuPolicy { DENY, ALLOW, ALLOW_ONCE }

data class SuLogEntryUi(
    val timestampMs: Long,
    val packageName: String,
    val uid: Int,
    val granted: Boolean,
    val command: String,
)

data class LogLine(
    val level: Int,    // 0=V 1=D 2=I 3=W 4=E
    val tag: String,
    val msg: String,
    val timestampMs: Long,
) {
    val levelName: String get() = when (level) {
        0 -> "V"; 1 -> "D"; 2 -> "I"; 3 -> "W"; 4 -> "E"; else -> "?"
    }
}
