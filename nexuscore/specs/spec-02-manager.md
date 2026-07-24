# NexusCore Spec 02 — NexusManager (Kotlin / Jetpack Compose)

| 字段 | 值 |
|---|---|
| Spec ID | NC-SPEC-02 |
| 版本 | 1.0.0 |
| 状态 | Approved (MVP baseline) |
| 适用阶段 | MVP（Phase 1） |
| 目标系统 | Android 14 / 15 / 16 (minSdk 34, targetSdk 35) |
| 语言/工具链 | Kotlin 2.0+, Jetpack Compose (BOM 2024.10+), Gradle 8.10 KTS |
| 依赖 | nexus.proto（见 Spec 01 §10.3）生成 Kotlin 类 |
| 严肃约束 | 客户端绝不直接执行 Root 命令；所有 Root 操作必须经 IPC → Daemon |

---

## 1. 目标与非目标

### 1.1 目标
1. 实现 NexusManager 与 NexusDaemon 的稳定 IPC 通信，具备**断线重连**与**连接池**。
2. 提供 3 个核心 Compose 页面：Dashboard、Modules、Logs。
3. 使用 MVVM + StateFlow，所有 IPC 调用走 `Dispatchers.IO`。
4. 支持本地 ZIP 模块安装（通过 IPC 把路径传给 Daemon）。
5. 实时流式订阅 Daemon 日志。

### 1.2 非目标（MVP 不做）
- 在线模块仓库浏览（仅保留入口按钮，点击后提示"敬请期待"）。
- 模块自升级、自动检查更新。
- 多 Daemon 实例管理。
- Root 授权管理 UI（依赖 Magisk/KSU/APatch 自己的 Manager）。

### 1.3 评审整改落点
| 评审问题 | 本 Spec 落点 |
|---|---|
| IPC 权限校验薄弱 | Manager 侧配合：启动握手时主动报告自身签名指纹（Daemon 端做权威校验，见 Spec 01 §10.2） |
| 断线重连 | §3.3 `ReconnectStrategy`，指数退避 + 系统事件触发 |
| UI 线程阻塞 | 所有 IPC 走 `Dispatchers.IO`，ViewModel 只暴露 `StateFlow` |

---

## 2. 目录结构

```
manager/
├── settings.gradle.kts
├── build.gradle.kts                 # 根项目
├── gradle/libs.versions.toml        # version catalog
└── app/
    ├── build.gradle.kts
    ├── proguard-rules.pro
    └── src/
        ├── main/
        │   ├── AndroidManifest.xml
        │   ├── kotlin/com/nexus/manager/
        │   │   ├── NexusApp.kt                 # Application
        │   │   ├── MainActivity.kt
        │   │   ├── ipc/
        │   │   │   ├── NexusIpcClient.kt       # 核心 IPC 客户端
        │   │   │   ├── IpcTransport.kt         # UDS 传输层
        │   │   │   ├── ProtobufCodec.kt        # 编解码 + length prefix
        │   │   │   ├── ReconnectStrategy.kt
        │   │   │   └── IpcException.kt
        │   │   ├── data/
        │   │   │   ├── model/                  # UI 侧 model（与 proto 解耦）
        │   │   │   ├── NexusRepository.kt
        │   │   │   └── LogStream.kt
        │   │   ├── viewmodel/
        │   │   │   ├── DashboardViewModel.kt
        │   │   │   ├── ModulesViewModel.kt
        │   │   │   └── LogsViewModel.kt
        │   │   └── ui/
        │   │       ├── theme/
        │   │       │   ├── Color.kt
        │   │       │   ├── Theme.kt
        │   │       │   └── Type.kt
        │   │       ├── components/             # 可复用 Composable
        │   │       └── pages/
        │   │           ├── DashboardPage.kt
        │   │           ├── ModulesPage.kt
        │   │           └── LogsPage.kt
        │   └── res/
        │       ├── values/strings.xml
        │       ├── drawable/
        │       └── mipmap-*/
        └── test/                                # 单元测试
```

---

## 3. IPC 客户端层

### 3.1 `ipc/IpcTransport.kt` —— UDS 传输

```kotlin
package com.nexus.manager.ipc

import android.net.LocalSocket
import android.net.LocalSocketAddress
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import java.io.IOException
import java.io.InputStream
import java.io.OutputStream

/**
 * 仅负责 UDS 连接与字节流读写。所有 frame 编解码在 ProtobufCodec 中做。
 * Daemon 在 /dev/socket/nexusd.sock 监听。
 */
class IpcTransport(
    private val socketPath: String = "/dev/socket/nexusd.sock"
) {
    enum class State { Disconnected, Connecting, Connected, Reconnecting }

    private val _state = MutableStateFlow(State.Disconnected)
    val state: StateFlow<State> = _state

    private var socket: LocalSocket? = null
    private var input: InputStream? = null
    private var output: OutputStream? = null

    @Throws(IOException::class)
    fun connect(timeoutMs: Int = 3000) {
        _state.value = State.Connecting
        val s = LocalSocket().apply {
            soTimeout = timeoutMs
            connect(LocalSocketAddress(socketPath, LocalSocketAddress.Namespace.FILESYSTEM))
        }
        socket = s
        input  = s.inputStream
        output = s.outputStream
        _state.value = State.Connected
    }

    fun read(buf: ByteArray, off: Int, len: Int): Int =
        input?.read(buf, off, len) ?: -1

    fun write(buf: ByteArray, off: Int, len: Int) {
        output?.write(buf, off, len)
        output?.flush()
    }

    fun close() {
        try { input?.close() } catch (_: IOException) {}
        try { output?.close() } catch (_: IOException) {}
        try { socket?.close() } catch (_: IOException) {}
        input = null; output = null; socket = null
        _state.value = State.Disconnected
    }
}
```

### 3.2 `ipc/ProtobufCodec.kt` —— 帧编解码

```kotlin
package com.nexus.manager.ipc

import com.nexus.manager.ipc.proto.Envelope
import java.io.IOException
import java.nio.ByteBuffer
import java.nio.ByteOrder

/**
 * 帧格式：[4B little-endian length][N bytes Envelope protobuf]
 */
object ProtobufCodec {

    private const val MAGIC: Int = 0x4E58434O  // 'NXCO'

    fun write(transport: IpcTransport, env: Envelope) {
        val payload = env.toByteArray()
        val header = ByteBuffer.allocate(4).order(ByteOrder.LITTLE_ENDIAN)
            .putInt(payload.size).array()
        transport.write(header, 0, 4)
        transport.write(payload, 0, payload.size)
    }

    @Throws(IOException::class)
    fun read(transport: IpcTransport): Envelope {
        val header = ByteArray(4)
        if (readFully(transport, header, 0, 4) != 4)
            throw IOException("EOF reading header")
        val len = ByteBuffer.wrap(header).order(ByteOrder.LITTLE_ENDIAN).int
        if (len <= 0 || len > MAX_FRAME) throw IOException("bad frame len: $len")

        val payload = ByteArray(len)
        if (readFully(transport, payload, 0, len) != len)
            throw IOException("EOF reading payload")
        val env = Envelope.parseFrom(payload)
        if (env.magic != MAGIC) throw IOException("bad magic")
        return env
    }

    private const val MAX_FRAME = 8 * 1024 * 1024  // 8 MiB

    private fun readFully(t: IpcTransport, b: ByteArray, off: Int, len: Int): Int {
        var read = 0
        while (read < len) {
            val n = t.read(b, off + read, len - read)
            if (n <= 0) break
            read += n
        }
        return read
    }
}
```

### 3.3 `ipc/ReconnectStrategy.kt` —— 指数退避 + 事件触发

> **整改：断线重连**。Daemon 可能被 LMK 杀死或重启，Manager 必须自愈。

```kotlin
package com.nexus.manager.ipc

import kotlinx.coroutines.delay
import kotlin.math.min

class ReconnectStrategy(
    private val baseDelayMs: Long = 500L,
    private val maxDelayMs: Long = 15_000L,
    private val maxAttempts: Int = Int.MAX_VALUE
) {
    suspend fun await(attempt: Int) {
        if (attempt >= maxAttempts) return
        // 指数退避 + 抖动
        val exp = (baseDelayMs shl attempt).coerceAtMost(maxDelayMs)
        val jitter = (0..exp / 4).random()
        delay(exp + jitter)
    }
}
```

### 3.4 `ipc/NexusIpcClient.kt` —— 核心客户端（连接池 + 自动重连）

```kotlin
package com.nexus.manager.ipc

import com.nexus.manager.ipc.proto.*
import kotlinx.coroutines.*
import kotlinx.coroutines.channels.Channel
import kotlinx.coroutines.flow.*
import kotlinx.coroutines.sync.Mutex
import kotlinx.coroutines.sync.withLock
import java.io.IOException

class NexusIpcClient(
    private val transport: IpcTransport = IpcTransport(),
    private val reconnect: ReconnectStrategy = ReconnectStrategy(),
    private val io: CoroutineDispatcher = Dispatchers.IO
) {
    sealed class Connection {
        object Connected : Connection()
        data class Reconnecting(val attempt: Int) : Connection()
        data class Failed(val cause: Throwable) : Connection()
    }

    private val _connection = MutableStateFlow<Connection>(Connection.Reconnecting(0))
    val connection: StateFlow<Connection> = _connection

    private val _events = MutableSharedFlow<Event>(extraBufferCapacity = 64)
    val events: SharedFlow<Event> = _events.asSharedFlow()

    private var nextSeq = 1
    private val pending = mutableMapOf<Int, CompletableDeferred<Response>>()
    private val pendingMu = Mutex()

    private val sendQueue = Channel<Envelope>(capacity = Channel.UNLIMITED)
    private var supervisor: Job? = null

    fun start(scope: CoroutineScope) {
        supervisor?.cancel()
        supervisor = scope.launch(io) { connectionLoop() }
    }

    fun stop() {
        supervisor?.cancel()
        transport.close()
    }

    /** 同步式请求-响应 */
    suspend fun request(req: Request): Response {
        val seq = nextSeq++
        val env = Envelope.newBuilder()
            .setMagic(0x4E58434O)
            .setVersion(1)
            .setSeq(seq)
            .setRequest(req)
            .build()
        val deferred = CompletableDeferred<Response>()
        pendingMu.withLock { pending[seq] = deferred }
        sendQueue.send(env)
        return withTimeoutOrNull(REQUEST_TIMEOUT_MS) { deferred.await() }
            ?: throw IpcException.Timeout("request $seq timed out")
    }

    private suspend fun connectionLoop() {
        var attempt = 0
        while (coroutineContext.isActive) {
            try {
                _connection.value = Connection.Reconnecting(attempt)
                transport.connect()
                _connection.value = Connection.Connected
                attempt = 0

                // 启动读写协程
                val reader = launch(io) { readerLoop() }
                val writer = launch(io) { writerLoop() }
                reader.join(); writer.cancel()
            } catch (e: IOException) {
                _connection.value = Connection.Reconnecting(attempt)
                attempt++
            } catch (e: CancellationException) {
                throw e
            } catch (e: Throwable) {
                _connection.value = Connection.Failed(e)
            } finally {
                transport.close()
                // 通知所有 pending 请求失败
                pendingMu.withLock {
                    pending.values.forEach { it.completeExceptionally(IpcException.Disconnected()) }
                    pending.clear()
                }
            }
            if (coroutineContext.isActive) reconnect.await(attempt)
        }
    }

    private suspend fun readerLoop() {
        while (coroutineContext.isActive) {
            val env = try { ProtobufCodec.read(transport) }
                      catch (e: IOException) { break }
            when (env.bodyCase) {
                Envelope.BodyCase.RESPONSE -> {
                    val resp = env.response
                    pendingMu.withLock { pending.remove(resp.seq)?.complete(resp) }
                        ?: continue   // 重复或过期响应，丢弃
                }
                Envelope.BodyCase.EVENT -> {
                    _events.emit(env.event)
                }
                else -> { /* 忽略 */ }
            }
        }
    }

    private suspend fun writerLoop() {
        for (env in sendQueue) {
            try { ProtobufCodec.write(transport, env) }
            catch (e: IOException) { break }
        }
    }

    companion object {
        private const val REQUEST_TIMEOUT_MS = 10_000L
    }
}
```

**关键点**：
- `sendQueue` 是无界 `Channel`，写入永不阻塞 UI 调用方。
- `pending` 用 `seq → CompletableDeferred` 配对响应。
- 断线时**所有 pending 立即失败**，避免调用方永久挂起。
- `events` 用 `SharedFlow`，多订阅者都能收到（Logs 页 + 通知组件）。

### 3.5 `ipc/IpcException.kt`

```kotlin
package com.nexus.manager.ipc

sealed class IpcException(message: String) : Exception(message) {
    class Timeout(msg: String) : IpcException(msg)
    class Disconnected : IpcException("connection lost")
    class Unauthorized(msg: String) : IpcException(msg)
    class ProtocolError(msg: String) : IpcException(msg)
}
```

---

## 4. 数据层（Repository）

### 4.1 `data/model/UiModels.kt` —— 与 Proto 解耦的 UI 模型

```kotlin
package com.nexus.manager.data.model

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
)

data class ModuleUi(
    val id: String,
    val name: String,
    val version: String,
    val author: String,
    val description: String,
    val enabled: Boolean,
    val priority: Int,
    val capabilities: List<String>,
)

data class LogLine(
    val level: Int,    // 0=V 1=D 2=I 3=W 4=E
    val tag: String,
    val msg: String,
    val timestampMs: Long,
)
```

### 4.2 `data/NexusRepository.kt`

```kotlin
package com.nexus.manager.data

import com.nexus.manager.data.model.*
import com.nexus.manager.ipc.NexusIpcClient
import com.nexus.manager.ipc.proto.*
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.flow.*
import kotlinx.coroutines.withContext

class NexusRepository(private val client: NexusIpcClient) {

    val connection = client.connection

    suspend fun ping(): Boolean = withContext(Dispatchers.IO) {
        runCatching {
            client.request(Request.newBuilder()
                .setPing(PingRequest.newBuilder().setToken("ping").build()).build())
        }.isSuccess
    }

    suspend fun getStatus(): SystemStatus? = withContext(Dispatchers.IO) {
        runCatching {
            val resp = client.request(Request.newBuilder()
                .setGetStatus(GetStatusRequest.getDefaultInstance()).build())
            if (resp.code != 0) return@runCatching null
            val s = resp.getStatus
            SystemStatus(
                rootAvailable = s.rootAvailable,
                rootProvider  = s.rootProvider,
                rootVersion   = s.rootVersion,
                selinuxEnforcing = s.selinuxEnforcing,
                selinuxDomain = s.selinuxDomain,
                daemonRunning = s.daemonRunning,
                daemonPid     = s.daemonPid,
                fsInterceptor = s.fsInterceptor,
                moduleCount   = s.moduleCount,
                safeMode      = s.safeMode,
                uptimeMs      = s.uptimeMs,
            )
        }.getOrNull()
    }

    suspend fun listModules(): List<ModuleUi> = withContext(Dispatchers.IO) {
        runCatching {
            val resp = client.request(Request.newBuilder()
                .setListModules(ListModulesRequest.getDefaultInstance()).build())
            if (resp.code != 0) emptyList()
            else resp.listModules.modulesList.map { m ->
                ModuleUi(
                    id = m.id, name = m.name, version = m.version,
                    author = m.author, description = m.description,
                    enabled = m.enabled, priority = m.priority,
                    capabilities = m.capabilitiesList.toList(),
                )
            }
        }.getOrDefault(emptyList())
    }

    suspend fun enableModule(id: String): Boolean = withContext(Dispatchers.IO) {
        runCatching { sendSimple(EnableModuleRequest.newBuilder().setId(id).build()) }.isSuccess
    }

    suspend fun disableModule(id: String): Boolean = withContext(Dispatchers.IO) {
        runCatching { sendSimple(DisableModuleRequest.newBuilder().setId(id).build()) }.isSuccess
    }

    suspend fun installModule(localPath: String): Boolean = withContext(Dispatchers.IO) {
        runCatching {
            val resp = client.request(Request.newBuilder()
                .setInstallModule(InstallModuleRequest.newBuilder()
                    .setLocalPath(localPath).build()).build())
            resp.code == 0
        }.getOrDefault(false)
    }

    suspend fun uninstallModule(id: String): Boolean = withContext(Dispatchers.IO) {
        runCatching { sendSimple(UninstallModuleRequest.newBuilder().setId(id).build()) }.isSuccess
    }

    suspend fun restartDaemon(): Boolean = withContext(Dispatchers.IO) {
        runCatching { sendSimple(RestartDaemonRequest.getDefaultInstance()) }.isSuccess
    }

    suspend fun enterSafeMode(timeoutSec: Int): Boolean = withContext(Dispatchers.IO) {
        runCatching {
            sendSimple(EnterSafeModeRequest.newBuilder().setTimeoutSec(timeoutSec).build())
        }.isSuccess
    }

    /** 订阅 Daemon 日志流，返回冷 Flow */
    fun subscribeLogs(minLevel: Int): Flow<LogLine> = client.events
        .filter { it.payloadCase == Event.PayloadCase.LOG_LINE }
        .map { e ->
            val l = e.logLine
            LogLine(l.level, l.tag, l.msg, e.timestampMs)
        }
        .filter { it.level >= minLevel }

    private suspend fun sendSimple(req: com.google.protobuf.Message) {
        val builder = Request.newBuilder()
        when (req) {
            is EnableModuleRequest    -> builder.setEnableModule(req)
            is DisableModuleRequest   -> builder.setDisableModule(req)
            is UninstallModuleRequest -> builder.setUninstallModule(req)
            is RestartDaemonRequest   -> builder.setRestartDaemon(req)
            is EnterSafeModeRequest   -> builder.setEnterSafeMode(req)
            else -> throw IllegalArgumentException("unsupported: ${req.javaClass}")
        }
        val resp = client.request(builder.build())
        if (resp.code != 0) throw IpcException.ProtocolError("code=${resp.code} msg=${resp.message}")
    }
}
```

---

## 5. ViewModel 层（MVVM + StateFlow）

### 5.1 `viewmodel/DashboardViewModel.kt`

```kotlin
package com.nexus.manager.viewmodel

import androidx.lifecycle.ViewModel
import androidx.lifecycle.viewModelScope
import com.nexus.manager.data.NexusRepository
import com.nexus.manager.data.model.SystemStatus
import com.nexus.manager.ipc.NexusIpcClient
import kotlinx.coroutines.flow.*
import kotlinx.coroutines.launch

class DashboardViewModel(private val repo: NexusRepository) : ViewModel() {

    private val _status = MutableStateFlow<SystemStatus?>(null)
    val status: StateFlow<SystemStatus?> = _status

    val connection = repo.connection.stateIn(
        viewModelScope, SharingStarted.WhileSubscribed(5_000), NexusIpcClient.Connection.Reconnecting(0)
    )

    private val _toast = MutableSharedFlow<String>()
    val toast = _toast.asSharedFlow()

    init {
        // 连接状态变化时自动拉取一次状态
        viewModelScope.launch {
            connection.collect { c ->
                if (c is NexusIpcClient.Connection.Connected) refresh()
            }
        }
    }

    fun refresh() = viewModelScope.launch {
        _status.value = repo.getStatus()
    }

    fun restartDaemon() = viewModelScope.launch {
        if (repo.restartDaemon()) _toast.emit("已请求重启 Daemon")
        else _toast.emit("重启请求失败")
    }

    fun enterSafeMode() = viewModelScope.launch {
        if (repo.enterSafeMode(0)) _toast.emit("已进入安全模式，重启后生效")
        else _toast.emit("进入安全模式失败")
    }
}
```

### 5.2 `viewmodel/ModulesViewModel.kt`

```kotlin
package com.nexus.manager.viewmodel

import androidx.lifecycle.ViewModel
import androidx.lifecycle.viewModelScope
import com.nexus.manager.data.NexusRepository
import com.nexus.manager.data.model.ModuleUi
import kotlinx.coroutines.flow.*
import kotlinx.coroutines.launch

class ModulesViewModel(private val repo: NexusRepository) : ViewModel() {

    private val _modules = MutableStateFlow<List<ModuleUi>>(emptyList())
    val modules: StateFlow<List<ModuleUi>> = _modules

    private val _loading = MutableStateFlow(false)
    val loading: StateFlow<Boolean> = _loading

    private val _toast = MutableSharedFlow<String>()
    val toast = _toast.asSharedFlow()

    fun refresh() = viewModelScope.launch {
        _loading.value = true
        _modules.value = repo.listModules()
        _loading.value = false
    }

    fun toggle(id: String, enable: Boolean) = viewModelScope.launch {
        val ok = if (enable) repo.enableModule(id) else repo.disableModule(id)
        if (ok) {
            _modules.value = _modules.value.map {
                if (it.id == id) it.copy(enabled = enable) else it
            }
            _toast.emit(if (enable) "已启用 $id，重启生效" else "已禁用 $id，重启生效")
        } else _toast.emit("操作失败")
    }

    fun install(localPath: String) = viewModelScope.launch {
        if (repo.installModule(localPath)) {
            _toast.emit("安装成功，需要重启")
            refresh()
        } else _toast.emit("安装失败")
    }

    fun uninstall(id: String) = viewModelScope.launch {
        if (repo.uninstallModule(id)) {
            _modules.value = _modules.value.filter { it.id != id }
            _toast.emit("已卸载 $id")
        } else _toast.emit("卸载失败")
    }
}
```

### 5.3 `viewmodel/LogsViewModel.kt`

```kotlin
package com.nexus.manager.viewmodel

import androidx.lifecycle.ViewModel
import androidx.lifecycle.viewModelScope
import com.nexus.manager.data.NexusRepository
import com.nexus.manager.data.model.LogLine
import kotlinx.coroutines.flow.*

class LogsViewModel(private val repo: NexusRepository) : ViewModel() {

    private val minLevel = MutableStateFlow(2) // 默认 Info+

    // 滑动窗口，避免内存爆炸
    val logs: Flow<List<LogLine>> = minLevel
        .flatMapLatest { level -> repo.subscribeLogs(level) }
        .runningFold(mutableListOf<LogLine>()) { acc, line ->
            acc.add(line)
            if (acc.size > 2000) acc.removeAt(0)
            acc
        }
        .map { it.toList() }
        .stateIn(viewModelScope, SharingStarted.WhileSubscribed(5_000), emptyList())

    fun setMinLevel(level: Int) { minLevel.value = level }
}
```

---

## 6. Compose UI

### 6.1 `MainActivity.kt`

```kotlin
package com.nexus.manager

import android.os.Bundle
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.activity.enableEdgeToEdge
import androidx.compose.foundation.layout.padding
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Modifier
import com.nexus.manager.data.NexusRepository
import com.nexus.manager.ipc.NexusIpcClient
import com.nexus.manager.ui.pages.*
import com.nexus.manager.ui.theme.NexusTheme
import com.nexus.manager.viewmodel.*

class MainActivity : ComponentActivity() {

    private lateinit var ipc: NexusIpcClient
    private lateinit var repo: NexusRepository

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        enableEdgeToEdge()

        ipc = NexusIpcClient().also { it.start(applicationScope) }
        repo = NexusRepository(ipc)

        setContent {
            NexusTheme {
                NexusRoot(repo)
            }
        }
    }

    override fun onDestroy() {
        super.onDestroy()
        ipc.stop()
    }

    private val applicationScope by lazy {
        androidScope()  // 来自 NexusApp 的 ProcessLifecycle绑定 scope
    }
}
```

### 6.2 `ui/NexusRoot.kt` —— Scaffold + 底部导航

```kotlin
package com.nexus.manager.ui

import androidx.compose.foundation.layout.*
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Modifier
import androidx.lifecycle.viewmodel.compose.viewModel
import com.nexus.manager.data.NexusRepository
import com.nexus.manager.ui.pages.*

@Composable
fun NexusRoot(repo: NexusRepository) {
    var current by remember { mutableStateOf(Page.Dashboard) }

    Scaffold(
        bottomBar = {
            NavigationBar {
                Page.entries.forEach { p ->
                    NavigationBarItem(
                        selected = current == p,
                        onClick = { current = p },
                        icon = { Icon(p.icon, contentDescription = p.label) },
                        label = { Text(p.label) },
                    )
                }
            }
        }
    ) { inner ->
        Box(Modifier.padding(inner)) {
            when (current) {
                Page.Dashboard -> DashboardPage(
                    viewModel { DashboardViewModel(repo) }
                )
                Page.Modules -> ModulesPage(
                    viewModel { ModulesViewModel(repo) }
                )
                Page.Logs -> LogsPage(
                    viewModel { LogsViewModel(repo) }
                )
            }
        }
    }
}

enum class Page(val label: String, val icon: androidx.compose.ui.graphics.vector.ImageVector) {
    Dashboard("状态", Icons.Default.Home),
    Modules("模块", Icons.Default.Extension),
    Logs("日志", Icons.Default.Article),
}
```

### 6.3 `ui/pages/DashboardPage.kt`

```kotlin
package com.nexus.manager.ui.pages

import androidx.compose.foundation.layout.*
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.unit.dp
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import com.nexus.manager.ipc.NexusIpcClient
import com.nexus.manager.viewmodel.DashboardViewModel

@Composable
fun DashboardPage(vm: DashboardViewModel) {
    val status by vm.status.collectAsStateWithLifecycle()
    val conn by vm.connection.collectAsStateWithLifecycle()
    val snackbar = remember { SnackbarHostState() }

    LaunchedEffect(Unit) {
        vm.toast.collect { snackbar.showSnackbar(it) }
    }
    LaunchedEffect(Unit) { vm.refresh() }

    Column(Modifier.fillMaxSize().padding(16.dp).verticalScroll(rememberScrollState())) {
        Text("NexusCore", style = MaterialTheme.typography.headlineMedium)
        Spacer(Modifier.height(4.dp))
        Text(
            when (conn) {
                is NexusIpcClient.Connection.Connected   -> "已连接 Daemon"
                is NexusIpcClient.Connection.Reconnecting -> "正在重连… (第 ${(conn as NexusIpcClient.Connection.Reconnecting).attempt} 次)"
                is NexusIpcClient.Connection.Failed       -> "连接失败: ${(conn as NexusIpcClient.Connection.Failed).cause.message}"
            },
            style = MaterialTheme.typography.bodyMedium,
            color = if (conn is NexusIpcClient.Connection.Connected)
                MaterialTheme.colorScheme.primary
            else MaterialTheme.colorScheme.error
        )

        Spacer(Modifier.height(16.dp))

        val s = status
        if (s == null) {
            Text("无法获取状态")
        } else {
            StatusRow("Root", "${s.rootProvider} ${s.rootVersion}", s.rootAvailable)
            StatusRow("SELinux", if (s.selinuxEnforcing) "Enforcing" else "Permissive",
                      s.selinuxEnforcing)
            StatusRow("SELinux 域", s.selinuxDomain, true)
            StatusRow("Daemon", "PID ${s.daemonPid}", s.daemonRunning)
            StatusRow("FS 拦截器", s.fsInterceptor, s.fsInterceptor != "noop")
            StatusRow("模块数量", s.moduleCount.toString(), s.moduleCount > 0)
            StatusRow("安全模式", if (s.safeMode) "已启用" else "关闭", !s.safeMode)
            StatusRow("运行时长", "${s.uptimeMs / 1000}s", true)
        }

        Spacer(Modifier.height(24.dp))
        Row(horizontalArrangement = Arrangement.spacedBy(12.dp)) {
            OutlinedButton(onClick = { vm.refresh() }) { Text("刷新") }
            Button(onClick = { vm.restartDaemon() }) { Text("重启 Daemon") }
            OutlinedButton(onClick = { vm.enterSafeMode() }) { Text("进入安全模式") }
        }
    }
    SnackbarHost(snackbar, modifier = Modifier.padding(16.dp))
}

@Composable
private fun StatusRow(label: String, value: String, ok: Boolean) {
    Surface(
        modifier = Modifier.fillMaxWidth().padding(vertical = 4.dp),
        tonalElevation = 1.dp
    ) {
        Row(Modifier.padding(12.dp), verticalAlignment = Alignment.CenterVertically) {
            Text(label, style = MaterialTheme.typography.bodyLarge, modifier = Modifier.weight(1f))
            Text(value, style = MaterialTheme.typography.bodyMedium)
            Spacer(Modifier.width(8.dp))
            Surface(
                color = if (ok) MaterialTheme.colorScheme.primaryContainer
                        else MaterialTheme.colorScheme.errorContainer,
                shape = MaterialTheme.shapes.small
            ) {
                Text(if (ok) "OK" else "WARN", modifier = Modifier.padding(horizontal = 8.dp, vertical = 2.dp))
            }
        }
    }
}
```

### 6.4 `ui/pages/ModulesPage.kt`

```kotlin
package com.nexus.manager.ui.pages

import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Delete
import androidx.compose.material.icons.filled.InstallMobile
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.unit.dp
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import com.nexus.manager.data.model.ModuleUi
import com.nexus.manager.viewmodel.ModulesViewModel

@Composable
fun ModulesPage(vm: ModulesViewModel) {
    val modules by vm.modules.collectAsStateWithLifecycle()
    val loading by vm.loading.collectAsStateWithLifecycle()

    val pickZip = rememberLauncherForActivityResult(
        ActivityResultContracts.OpenDocument()
    ) { uri ->
        uri?.let {
            // 把 URI 复制到 Daemon 可读的临时路径，再传给 installModule
            // 见 §6.5 FileBridge
        }
    }

    LaunchedEffect(Unit) { vm.refresh() }

    Column(Modifier.fillMaxSize()) {
        TopAppBar(title = { Text("已安装模块") })
        Row(Modifier.padding(12.dp), horizontalArrangement = Arrangement.spacedBy(8.dp)) {
            Button(onClick = { pickZip.launch(arrayOf("application/zip")) }) {
                Icon(Icons.Default.InstallMobile, null); Spacer(Modifier.width(4.dp)); Text("从本地 ZIP 安装")
            }
            OutlinedButton(onClick = { /* 在线仓库 Phase2 */ }) { Text("从存储库安装") }
        }
        if (loading && modules.isEmpty()) {
            Box(Modifier.fillMaxSize(), contentAlignment = Alignment.Center) {
                CircularProgressIndicator()
            }
        } else if (modules.isEmpty()) {
            Box(Modifier.fillMaxSize(), contentAlignment = Alignment.Center) {
                Text("暂无模块，点上方按钮安装", style = MaterialTheme.typography.bodyMedium)
            }
        } else {
            LazyColumn(
                Modifier.weight(1f),
                contentPadding = PaddingValues(12.dp),
                verticalArrangement = Arrangement.spacedBy(8.dp)
            ) {
                items(modules, key = { it.id }) { m -> ModuleCard(m, vm) }
            }
        }
    }
}

@Composable
private fun ModuleCard(m: ModuleUi, vm: ModulesViewModel) {
    ElevatedCard(Modifier.fillMaxWidth()) {
        Column(Modifier.padding(12.dp)) {
            Row(verticalAlignment = Alignment.CenterVertically) {
                Text(m.name, style = MaterialTheme.typography.titleMedium, modifier = Modifier.weight(1f))
                Switch(checked = m.enabled, onCheckedChange = { vm.toggle(m.id, it) })
            }
            Text("${m.author} · v${m.version} · priority=${m.priority}",
                 style = MaterialTheme.typography.bodySmall,
                 color = MaterialTheme.colorScheme.onSurfaceVariant)
            Spacer(Modifier.height(4.dp))
            Text(m.description.ifEmpty { "(无描述)" }, style = MaterialTheme.typography.bodyMedium)
            if (m.capabilities.isNotEmpty()) {
                Spacer(Modifier.height(4.dp))
                Text("能力: ${m.capabilities.joinToString(", ")}",
                     style = MaterialTheme.typography.labelSmall)
            }
            Row(Modifier.padding(top = 8.dp), horizontalArrangement = Arrangement.End) {
                TextButton(onClick = { vm.uninstall(m.id) }) {
                    Icon(Icons.Default.Delete, null); Spacer(Modifier.width(4.dp)); Text("卸载")
                }
            }
        }
    }
}
```

### 6.5 `data/FileBridge.kt` —— URI → 本地路径桥接

```kotlin
package com.nexus.manager.data

import android.content.Context
import android.net.Uri
import java.io.File

/**
 * Daemon 以 root 运行，无权访问 app 的 SAF URI。
 * 把 ZIP 复制到 /data/local/tmp/nexus_install_<rand>.zip，
 * 由 Daemon 直接 open() 该路径。
 */
object FileBridge {
    fun copyToTmp(context: Context, uri: Uri): String? {
        return runCatching {
            val tmp = File(context.cacheDir, "nexus_install_${System.currentTimeMillis()}.zip")
            context.contentResolver.openInputStream(uri)?.use { input ->
                tmp.outputStream().use { input.copyTo(it) }
            } ?: return null
            // 再复制到 /data/local/tmp（Daemon 必可读）
            val target = File("/data/local/tmp/${tmp.name}")
            tmp.copyTo(target, overwrite = true)
            tmp.delete()
            target.absolutePath
        }.getOrNull()
    }
}
```

> 在 `ModulesPage` 的 `pickZip` 回调中调用：
> ```kotlin
> val path = FileBridge.copyToTmp(context, it)
> if (path != null) vm.install(path)
> ```

### 6.6 `ui/pages/LogsPage.kt` —— 实时日志流

```kotlin
package com.nexus.manager.ui.pages

import androidx.compose.foundation.layout.*
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.lazy.rememberLazyListState
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.unit.dp
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import com.nexus.manager.data.model.LogLine
import com.nexus.manager.viewmodel.LogsViewModel
import kotlinx.coroutines.launch

@Composable
fun LogsPage(vm: LogsViewModel) {
    val logs by vm.logs.collectAsStateWithLifecycle(emptyList())
    val listState = rememberLazyListState()
    val scope = rememberCoroutineScope()

    // 新日志到达时自动滚到底
    LaunchedEffect(logs.size) {
        if (logs.isNotEmpty()) listState.animateScrollToItem(logs.size - 1)
    }

    Column(Modifier.fillMaxSize()) {
        TopAppBar(title = { Text("Daemon 日志") }, actions = {
            var menuOpen by remember { mutableStateOf(false) }
            TextButton(onClick = { menuOpen = true }) { Text("级别") }
            DropdownMenu(expanded = menuOpen, onDismissRequest = { menuOpen = false }) {
                listOf(0 to "Verbose", 1 to "Debug", 2 to "Info", 3 to "Warn", 4 to "Error").forEach { (l, n) ->
                    DropdownMenuItem(text = { Text(n) }, onClick = {
                        vm.setMinLevel(l); menuOpen = false
                    })
                }
            }
        })
        LazyColumn(
            state = listState,
            modifier = Modifier.fillMaxSize(),
            contentPadding = PaddingValues(8.dp)
        ) {
            items(logs) { l -> LogLineItem(l) }
        }
    }
}

@Composable
private fun LogLineItem(l: LogLine) {
    val color = when (l.level) {
        4 -> Color(0xFFE53935)
        3 -> Color(0xFFFB8C00)
        2 -> Color(0xFF1E88E5)
        1 -> Color(0xFF8E8E8E)
        else -> Color(0xFFBDBDBD)
    }
    Text(
        text = "[${levelName(l.level)}] ${l.tag}: ${l.msg}",
        color = color,
        fontFamily = FontFamily.Monospace,
        style = MaterialTheme.typography.bodySmall,
        modifier = Modifier.padding(vertical = 1.dp)
    )
}

private fun levelName(l: Int) = when (l) {
    0 -> "V"; 1 -> "D"; 2 -> "I"; 3 -> "W"; 4 -> "E"; else -> "?"
}
```

---

## 7. Theme

### `ui/theme/Color.kt`

```kotlin
package com.nexus.manager.ui.theme

import androidx.compose.ui.graphics.Color

// 深色为主，呼应系统级工具调性
val NexusDark = darkColorScheme(
    primary = Color(0xFF00E5FF),
    onPrimary = Color(0xFF000000),
    primaryContainer = Color(0xFF003B44),
    onPrimaryContainer = Color(0xFF6FF3FF),
    background = Color(0xFF0B0F14),
    surface = Color(0xFF11161C),
    onSurface = Color(0xFFE0E6ED),
    error = Color(0xFFFF5252),
)
```

### `ui/theme/Theme.kt`

```kotlin
package com.nexus.manager.ui.theme

import androidx.compose.material3.*
import androidx.compose.runtime.Composable

@Composable
fun NexusTheme(content: @Composable () -> Unit) {
    MaterialTheme(
        colorScheme = NexusDark,
        typography = Typography(),
        content = content
    )
}
```

---

## 8. Gradle 配置

### 8.1 `manager/settings.gradle.kts`

```kotlin
pluginManagement {
    repositories {
        google()
        mavenCentral()
        gradlePluginPortal()
    }
}
dependencyResolutionManagement {
    repositoriesMode.set(RepositoriesMode.FAIL_ON_PROJECT_REPOS)
    repositories {
        google()
        mavenCentral()
    }
}
rootProject.name = "NexusManager"
include(":app")
```

### 8.2 `manager/gradle/libs.versions.toml`

```toml
[versions]
agp = "8.7.0"
kotlin = "2.0.21"
composeBom = "2024.10.01"
lifecycle = "2.8.7"
coroutines = "1.9.0"
protobuf = "3.25.5"
protobufPlugin = "0.9.4"

[libraries]
androidx-core-ktx = "androidx.core:core-ktx:1.13.1"
androidx-lifecycle-runtime = { module = "androidx.lifecycle:lifecycle-runtime-ktx", version.ref = "lifecycle" }
androidx-lifecycle-viewmodel-compose = { module = "androidx.lifecycle:lifecycle-viewmodel-compose", version.ref = "lifecycle" }
androidx-lifecycle-runtime-compose = { module = "androidx.lifecycle:lifecycle-runtime-compose", version.ref = "lifecycle" }
androidx-activity-compose = "androidx.activity:activity-compose:1.9.3"
compose-bom = { module = "androidx.compose:compose-bom", version.ref = "composeBom" }
compose-ui = { module = "androidx.compose.ui:ui" }
compose-ui-graphics = { module = "androidx.compose.ui:ui-graphics" }
compose-ui-tooling-preview = { module = "androidx.compose.ui:ui-tooling-preview" }
compose-material3 = { module = "androidx.compose.material3:material3" }
compose-material-icons-extended = { module = "androidx.compose.material:material-icons-extended" }
kotlinx-coroutines-android = { module = "org.jetbrains.kotlinx:kotlinx-coroutines-android", version.ref = "coroutines" }
protobuf-javalite = { module = "com.google.protobuf:protobuf-javalite", version.ref = "protobuf" }

[plugins]
android-application = { id = "com.android.application", version.ref = "agp" }
kotlin-android = { id = "org.jetbrains.kotlin.android", version.ref = "kotlin" }
compose-compiler = { id = "org.jetbrains.kotlin.plugin.compose", version.ref = "kotlin" }
protobuf = { id = "com.google.protobuf", version.ref = "protobufPlugin" }
```

### 8.3 `manager/app/build.gradle.kts`

```kotlin
import com.google.protobuf.gradle.id

plugins {
    alias(libs.plugins.android.application)
    alias(libs.plugins.kotlin.android)
    alias(libs.plugins.compose.compiler)
    alias(libs.plugins.protobuf)
}

android {
    namespace = "com.nexus.manager"
    compileSdk = 35

    defaultConfig {
        applicationId = "com.nexus.manager"
        minSdk = 34
        targetSdk = 35
        versionCode = 1
        versionName = "1.0.0"
        ndk { abiFilters += listOf("arm64-v8a", "x86_64") }
    }

    buildFeatures { compose = true }
    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }
    kotlinOptions { jvmTarget = "17" }

    packaging {
        resources.excludes += setOf(
            "META-INF/AL2.0", "META-INF/LGPL2.1",
            "META-INF/DEPENDENCIES", "META-INF/*.kotlin_module"
        )
    }

    buildTypes {
        release {
            isMinifyEnabled = true
            isShrinkResources = true
            proguardFiles(
                getDefaultProguardFile("proguard-android-optimize.txt"),
                "proguard-rules.pro"
            )
            signingConfig = signingConfigs.getByName("debug") // MVP；正式需自建 keystore
        }
    }
}

dependencies {
    implementation(libs.androidx.core.ktx)
    implementation(libs.androidx.lifecycle.runtime)
    implementation(libs.androidx.lifecycle.viewmodel.compose)
    implementation(libs.androidx.lifecycle.runtime.compose)
    implementation(libs.androidx.activity.compose)

    implementation(platform(libs.compose.bom))
    implementation(libs.compose.ui)
    implementation(libs.compose.ui.graphics)
    implementation(libs.compose.ui.tooling.preview)
    implementation(libs.compose.material3)
    implementation(libs.compose.material.icons.extended)

    implementation(libs.kotlinx.coroutines.android)
    implementation(libs.protobuf.javalite)
}

protobuf {
    protoc {
        artifact = "com.google.protobuf:protoc:${libs.versions.protobuf.get()}"
    }
    generateProtoTasks {
        all().forEach {
            it.builtins {
                id("java") {
                    option("lite")
                }
            }
        }
    }
    // schema 来源：本仓库 daemon/proto/nexus.proto（用 symlink 或 copy）
    // 推荐：在 app/src/main/proto/ 下放一份副本
}
```

### 8.4 `manager/app/proguard-rules.pro`（关键保留）

```proguard
# Protobuf 生成类
-keep class com.nexus.manager.ipc.proto.** { *; }
-keepclassmembers class com.nexus.manager.ipc.proto.** { *; }

# Coroutines
-keepclassmembers class kotlinx.coroutines.** { *; }
```

### 8.5 `AndroidManifest.xml`

```xml
<?xml version="1.0" encoding="utf-8"?>
<manifest xmlns:android="http://schemas.android.com/apk/res/android">

    <!-- 读取 /dev/socket/nexusd.sock 不需要 INTERNET 权限（UDS） -->
    <!-- 但写入 /data/local/tmp 需要 root，由 Daemon 间接完成 -->

    <application
        android:name=".NexusApp"
        android:allowBackup="false"
        android:icon="@mipmap/ic_launcher"
        android:label="@string/app_name"
        android:roundIcon="@mipmap/ic_launcher_round"
        android:supportsRtl="true"
        android:theme="@style/Theme.NexusManager">
        <activity
            android:name=".MainActivity"
            android:exported="true"
            android:label="@string/app_name">
            <intent-filter>
                <action android:name="android.intent.action.MAIN" />
                <category android:name="android.intent.category.LAUNCHER" />
            </intent-filter>
        </activity>
    </application>
</manifest>
```

### 8.6 `NexusApp.kt`

```kotlin
package com.nexus.manager

import android.app.Application
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.SupervisorJob

class NexusApp : Application() {
    val appScope = CoroutineScope(SupervisorJob() + Dispatchers.Default)
}
```

---

## 9. 验收标准

- [ ] `./gradlew :app:assembleRelease` 成功，产出 `app-release.apk`
- [ ] APK 安装后能启动到 Dashboard
- [ ] 启动后 5s 内自动连接 Daemon（若已部署）
- [ ] Dashboard 正确显示 Root/SELinux/Daemon 状态
- [ ] Modules 页能列出模块、切换开关、卸载、从本地 ZIP 安装
- [ ] Logs 页能实时滚动显示 Daemon 日志，新日志自动滚到底
- [ ] 主动 `adb shell kill -9 <nexusd pid>` 后，Manager 在 30s 内自动重连并刷新状态
- [ ] 陌生 APP（不同签名）尝试连 UDS 时被 Daemon 拒绝（验证见 Spec 01 §12.3 第 7 项）
- [ ] 所有 IPC 调用都在 `Dispatchers.IO`，UI 滚动无卡顿（用布局检查器验证）

---

## 10. 风险与缓解

| 风险 | 缓解 |
|---|---|
| Daemon 未启动时 Manager 卡在"重连中" | UI 明确显示重连次数；提供"手动重试"按钮 |
| SAF URI 无法被 Daemon 直接读取 | `FileBridge` 复制到 `/data/local/tmp` |
| 日志风暴导致 OOM | `LogsViewModel` 滑动窗口 2000 行 |
| Proto schema 不一致 | schema 单一来源 `daemon/proto/nexus.proto`，Manager 通过 copy 同步 |
| Compose 在低性能机器卡顿 | 仅用 Material3 默认动画，禁用复杂 transition |

---

## 11. 与其它 Spec 的依赖

- IPC schema 完全依赖 [Spec 01 §10.3](./spec-01-daemon.md#10-ipc-server-与凭证校验)。
- 模块卡片显示的 `capabilities` 字段语义见 [Spec 03 §2](./spec-03-module-sdk.md)。
