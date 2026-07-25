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
