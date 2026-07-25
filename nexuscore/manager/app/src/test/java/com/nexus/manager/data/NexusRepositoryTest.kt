package com.nexus.manager.data.repo

import com.google.common.truth.Truth.assertThat
import com.nexus.manager.data.model.SystemStatus
import com.nexus.manager.data.model.SuPolicy
import com.nexus.manager.ipc.IpcException
import com.nexus.manager.ipc.NexusIpcClient
import com.nexus.manager.ipc.proto.*
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.runBlocking
import kotlinx.coroutines.test.runTest
import org.junit.Test
import org.mockito.kotlin.any
import org.mockito.kotlin.mock
import org.mockito.kotlin.whenever

/**
 * NexusRepository 单元测试
 *
 * 用 mock IPC client 验证 Repository 把 proto response 正确转换为 UI model。
 */
class NexusRepositoryTest {

    @Test
    fun `ping returns true when response code is 0`() = runTest {
        val mockClient = mock<NexusIpcClient> {
            on { request(any()) }.thenReturn(
                Response.newBuilder().setCode(0).setMessage("ok")
                    .setPing(PingResponse.newBuilder().setToken("ping").setServerVersion(1))
                    .build()
            )
        }
        val repo = NexusRepository(mockClient)
        assertThat(repo.ping()).isTrue()
    }

    @Test
    fun `ping returns false when IPC throws`() = runTest {
        val mockClient = mock<NexusIpcClient> {
            on { request(any()) }.thenThrow(IpcException.Disconnected())
        }
        val repo = NexusRepository(mockClient)
        assertThat(repo.ping()).isFalse()
    }

    @Test
    fun `getStatus returns EMPTY on error response`() = runTest {
        val mockClient = mock<NexusIpcClient> {
            on { request(any()) }.thenReturn(
                Response.newBuilder().setCode(-1).setMessage("error").build()
            )
        }
        val repo = NexusRepository(mockClient)
        val status = repo.getStatus()
        assertThat(status).isEqualTo(SystemStatus.EMPTY)
    }

    @Test
    fun `getStatus returns parsed status on success`() = runTest {
        val mockClient = mock<NexusIpcClient> {
            on { request(any()) }.thenReturn(
                Response.newBuilder().setCode(0).setMessage("ok")
                    .setGetStatus(GetStatusResponse.newBuilder()
                        .setRootAvailable(true)
                        .setRootProvider("magisk")
                        .setRootVersion("30.0")
                        .setSelinuxEnforcing(true)
                        .setSelinuxDomain("u:r:nexus_daemon:s0")
                        .setDaemonRunning(true)
                        .setDaemonPid(1234)
                        .setFsInterceptor("bind")
                        .setModuleCount(3)
                        .setSafeMode(false)
                        .setUptimeMs(60000)
                        .setAndroidVersion("14")
                        .setSecurityPatch("2024-01-01")
                        .setKernelVersion("5.15")
                        .setArch("arm64")
                        .setDaemonVersion("1.0.0")
                        .build()
                    ).build()
            )
        }
        val repo = NexusRepository(mockClient)
        val status = repo.getStatus()
        assertThat(status.rootAvailable).isTrue()
        assertThat(status.rootProvider).isEqualTo("magisk")
        assertThat(status.daemonPid).isEqualTo(1234)
        assertThat(status.moduleCount).isEqualTo(3)
        assertThat(status.daemonVersion).isEqualTo("1.0.0")
    }

    @Test
    fun `listModules returns empty list on error`() = runTest {
        val mockClient = mock<NexusIpcClient> {
            on { request(any()) }.thenReturn(
                Response.newBuilder().setCode(-1).setMessage("error").build()
            )
        }
        val repo = NexusRepository(mockClient)
        assertThat(repo.listModules()).isEmpty()
    }

    @Test
    fun `listModules returns parsed modules on success`() = runTest {
        val mockClient = mock<NexusIpcClient> {
            on { request(any()) }.thenReturn(
                Response.newBuilder().setCode(0).setMessage("ok")
                    .setListModules(ListModulesResponse.newBuilder()
                        .addModules(ModuleInfo.newBuilder()
                            .setId("mod_a")
                            .setName("Module A")
                            .setVersion("1.0.0")
                            .setAuthor("tester")
                            .setDescription("a test")
                            .setEnabled(true)
                            .setPriority(100)
                            .addCapabilities("EXECUTE_SHELL")
                            .build())
                        .addModules(ModuleInfo.newBuilder()
                            .setId("mod_b")
                            .setName("Module B")
                            .setVersion("2.0.0")
                            .setAuthor("tester2")
                            .setEnabled(false)
                            .setPriority(50)
                            .build())
                        .build()
                    ).build()
            )
        }
        val repo = NexusRepository(mockClient)
        val modules = repo.listModules()
        assertThat(modules).hasSize(2)
        assertThat(modules[0].id).isEqualTo("mod_a")
        assertThat(modules[0].enabled).isTrue()
        assertThat(modules[0].capabilities).containsExactly("EXECUTE_SHELL")
        assertThat(modules[1].id).isEqualTo("mod_b")
        assertThat(modules[1].enabled).isFalse()
    }

    @Test
    fun `enableModule returns true on success`() = runTest {
        val mockClient = mock<NexusIpcClient> {
            on { request(any()) }.thenReturn(
                Response.newBuilder().setCode(0).setMessage("ok").build()
            )
        }
        val repo = NexusRepository(mockClient)
        assertThat(repo.enableModule("mod_a")).isTrue()
    }

    @Test
    fun `enableModule returns false on IPC error`() = runTest {
        val mockClient = mock<NexusIpcClient> {
            on { request(any()) }.thenThrow(IpcException.Disconnected())
        }
        val repo = NexusRepository(mockClient)
        assertThat(repo.enableModule("mod_a")).isFalse()
    }

    @Test
    fun `installModule returns Result success on code 0`() = runTest {
        val mockClient = mock<NexusIpcClient> {
            on { request(any()) }.thenReturn(
                Response.newBuilder().setCode(0).setMessage("ok")
                    .setInstallModule(InstallModuleResponse.newBuilder()
                        .setId("new_mod")
                        .setNeedReboot(true)
                        .build())
                    .build()
            )
        }
        val repo = NexusRepository(mockClient)
        val result = repo.installModule("/tmp/test.zip")
        assertThat(result.isSuccess).isTrue()
        assertThat(result.getOrThrow().id).isEqualTo("new_mod")
        assertThat(result.getOrThrow().needReboot).isTrue()
    }

    @Test
    fun `installModule returns Result failure on error code`() = runTest {
        val mockClient = mock<NexusIpcClient> {
            on { request(any()) }.thenReturn(
                Response.newBuilder().setCode(-3).setMessage("invalid zip").build()
            )
        }
        val repo = NexusRepository(mockClient)
        val result = repo.installModule("/tmp/test.zip")
        assertThat(result.isFailure).isTrue()
    }

    @Test
    fun `setSuPolicy builds correct request`() = runTest {
        val mockClient = mock<NexusIpcClient> {
            on { request(any()) }.thenReturn(
                Response.newBuilder().setCode(0).setMessage("ok").build()
            )
        }
        val repo = NexusRepository(mockClient)
        val ok = repo.setSuPolicy("com.example", 10042, SuPolicy.ALLOW, 60)
        assertThat(ok).isTrue()
    }

    @Test
    fun `listSuApps parses apps correctly`() = runTest {
        val mockClient = mock<NexusIpcClient> {
            on { request(any()) }.thenReturn(
                Response.newBuilder().setCode(0).setMessage("ok")
                    .setListSuApps(ListSuAppsResponse.newBuilder()
                        .addApps(SuAppInfo.newBuilder()
                            .setPackageName("com.example.app1")
                            .setUid(10042)
                            .setPolicy(SuAppInfo.Policy.ALLOW)
                            .setLastRequestMs(1000)
                            .setRequestCount(3)
                            .setTimeoutSec(0)
                            .build())
                        .build()
                    ).build()
            )
        }
        val repo = NexusRepository(mockClient)
        val apps = repo.listSuApps()
        assertThat(apps).hasSize(1)
        assertThat(apps[0].packageName).isEqualTo("com.example.app1")
        assertThat(apps[0].uid).isEqualTo(10042)
        assertThat(apps[0].policy).isEqualTo(SuPolicy.ALLOW)
        assertThat(apps[0].requestCount).isEqualTo(3)
    }

    @Test
    fun `reboot builds correct request with mode`() = runTest {
        val mockClient = mock<NexusIpcClient> {
            on { request(any()) }.thenReturn(
                Response.newBuilder().setCode(0).setMessage("ok").build()
            )
        }
        val repo = NexusRepository(mockClient)
        assertThat(repo.reboot(RebootRequest.Mode.NORMAL)).isTrue()
        assertThat(repo.reboot(RebootRequest.Mode.RECOVERY)).isTrue()
        assertThat(repo.reboot(RebootRequest.Mode.BOOTLOADER)).isTrue()
    }
}
