package com.nexus.manager

import android.app.Application
import com.nexus.manager.data.repo.NexusRepository
import com.nexus.manager.data.settings.SettingsStore
import com.nexus.manager.ipc.NexusIpcClient
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.SupervisorJob

/**
 * NexusCore Manager Application
 *
 * 充当轻量级 ServiceLocator：
 * - appScope：进程级协程作用域，用于 IPC 客户端后台连接循环
 * - ipcClient / repository / settings：单例，全局共享
 */
class NexusApp : Application() {

    val appScope: CoroutineScope = CoroutineScope(SupervisorJob() + Dispatchers.Default)

    lateinit var ipcClient: NexusIpcClient
        private set

    lateinit var repository: NexusRepository
        private set

    lateinit var settings: SettingsStore
        private set

    override fun onCreate() {
        super.onCreate()
        instance = this

        settings = SettingsStore(this)
        ipcClient = NexusIpcClient().also { it.start(appScope) }
        repository = NexusRepository(ipcClient)
    }

    companion object {
        @Volatile
        private var instance: NexusApp? = null

        fun get(): NexusApp =
            instance ?: error("NexusApp not initialized")
    }
}
