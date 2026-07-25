/*
 * AutoVeil / NexusCore
 * Copyright (c) 2026 AutoVeil / NexusCore Contributors
 *
 * Licensed under the MIT License with Non-Commercial Clause (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License in the LICENSE file at the project root.
 *
 * Commercial use requires a separate written license from the copyright holders.
 */

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
