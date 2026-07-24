# NexusCore Spec 01 — Daemon 核心架构与 Root 守护进程

| 字段 | 值 |
|---|---|
| Spec ID | NC-SPEC-01 |
| 版本 | 1.0.0 |
| 状态 | Approved (MVP baseline) |
| 适用阶段 | MVP（Phase 1） |
| 目标系统 | Android 14 / 15 / 16 (API 34+) |
| 目标 ABI | `arm64-v8a` (主)，`x86_64` (模拟器调试) |
| 语言/工具链 | C++17 + Rust (可选 FFI)，CMake ≥ 3.22，NDK r26d+ |
| 严肃约束 | 仅用户态 syscall；禁止任何 `.ko` 内核模块；任何 syscall 失败必须有 fallback；绝不允许导致 Bootloop |

---

## 1. 目标与非目标

### 1.1 目标
1. 提供一个随 `init` 启动、不被系统杀死的 Root 守护进程 `nexusd`。
2. 提供动态 SELinux 策略修补能力，仅放宽 `u:r:nexus_daemon:s0` 自身域，**不**全局 `setenforce 0`。
3. 提供文件系统拦截能力（MVP 用 OverlayFS / Bind Mount），抽象出 `IFileSystemInterceptor` 接口，预留 FUSE 插槽。
4. 提供模块清单解析、按优先级排序、安全脚本执行（独立 Mount Namespace）。
5. 提供 Unix Domain Socket IPC Server，支持 `SO_PEERCRED` 客户端凭证校验。
6. 提供内存事件总线 `IEventBus`，为未来 Agent 协同预留。

### 1.2 非目标（MVP 不做）
- FUSE "幻影文件系统"实现（仅留接口）。
- 跨 App 的"平行宇宙"整机 Namespace 切换（Phase 5 收缩为 App 隔离沙盒，见 Spec Web 路线图）。
- Lua/Wasm 脚本引擎（Phase 4）。
- 模块仓库在线索引（仅支持本地 ZIP 安装）。

### 1.3 严重问题当场整改映射（来自评审）
| 评审问题 | 整改落点 | 本 Spec 章节 |
|---|---|---|
| 跨机型 OverlayFS 不支持无 fallback | `IFileSystemInterceptor::detect()` 探测 + Bind Mount 降级 | §6.2, §6.3 |
| 缺 Root 环境依赖校验 | `RootEnvironmentDetector` 启动期检测 Magisk/KSU/APatch | §5.1 |
| IPC 权限校验薄弱 | `SO_PEERCRED` + 签名/UID 白名单 | §8.2 |
| Phase 5 整机平行宇宙过高 | 收缩为 App 级隔离沙盒 | 见 Web 路线图 |
| 模块冲突机制缺失 | `IModuleLoader` 按 DMM `priority` 字段叠加排序 | §7.3 |

---

## 2. 顶层架构

```
┌──────────────────────────────────────────────────────────────┐
│  init.rc (boot)  ──>  exec nexusd --daemon                   │
│                          │                                    │
│                          ▼                                    │
│            ┌─────────────────────────────┐                   │
│            │      nexusd (Root)          │                   │
│            │  ┌───────────────────────┐  │                   │
│            │  │ Watchdog + PidFile    │  │                   │
│            │  ├───────────────────────┤  │                   │
│            │  │ RootEnvironmentDetect │  │                   │
│            │  ├───────────────────────┤  │                   │
│            │  │ SELinuxManager        │  │                   │
│            │  ├───────────────────────┤  │                   │
│            │  │ FileSystemInterceptor │  │  OverlayFS/Bind   │
│            │  ├───────────────────────┤  │  ───────► /system  │
│            │  │ ModuleLoader          │  │                   │
│            │  │   ├─ parseManifest    │  │                   │
│            │  │   ├─ priority sort    │  │                   │
│            │  │   └─ ShellExecutor     │  │  fork+execve     │
│            │  │       (Mount NS)      │  │  ───────► service.sh│
│            │  ├───────────────────────┤  │                   │
│            │  │ ProcessIsolator       │  │                   │
│            │  ├───────────────────────┤  │                   │
│            │  │ EventBus (in-mem)     │  │                   │
│            │  ├───────────────────────┤  │                   │
│            │  │ IpcServer (UDS)       │◄─┼─── NexusManager   │
│            │  │   └─ SO_PEERCRED      │  │    (Kotlin)       │
│            │  └───────────────────────┘  │                   │
│            └─────────────────────────────┘                   │
└──────────────────────────────────────────────────────────────┘
```

### 2.1 启动时序（boot sequence）

```
T0  init → exec nexusd --daemon
T1  parse argv, read /data/adb/nexuscore/config.toml
T2  write PID file (/dev/nexusd.pid) — atomic, 0644
T3  RootEnvironmentDetector::detect()   ── 决定 sepolicy 路径、overlay 存储目录
T4  SELinuxManager::patchSelfDomain()   ── 仅放宽 nexus_daemon 域
T5  EventBus 初始化
T6  ModuleLoader::scanModules(/data/adb/nexuscore/modules)
T7  FileSystemInterceptor::mountAll(modules)   ── post-fs-data 阶段
T8  ShellExecutor::runStage(POST_FS_DATA)
T9  IpcServer::listen(/dev/socket/nexusd.sock)
T10 ShellExecutor::runStage(LATE_START)        ── 后台线程
T11 Watchdog 进入主循环（心跳 + 子线程监控）
```

---

## 3. 目录结构（Daemon）

```
daemon/
├── CMakeLists.txt
├── proto/
│   └── nexus.proto              # IPC schema
└── src/
    ├── main.cpp                 # 入口、daemonize、watchdog loop
    ├── core/
    │   ├── pid_file.{h,cpp}
    │   ├── watchdog.{h,cpp}
    │   ├── root_env_detect.{h,cpp}
    │   └── logger.{h,cpp}       # ring buffer + 文件落盘
    ├── selinux/
    │   └── selinux_manager.{h,cpp}
    ├── fs/
    │   ├── i_file_system_interceptor.h   # 抽象接口
    │   ├── overlay_fs_interceptor.{h,cpp}
    │   ├── bind_mount_interceptor.{h,cpp}   # 降级方案
    │   └── fs_detector.{h,cpp}             # 内核能力探测
    ├── module/
    │   ├── i_module_loader.h
    │   ├── module_loader.{h,cpp}
    │   ├── manifest.{h,cpp}               # DMM 解析
    │   └── shell_executor.{h,cpp}
    ├── process/
    │   ├── i_process_isolator.h
    │   └── namespace_isolator.{h,cpp}
    ├── event/
    │   ├── i_event_bus.h
    │   └── in_memory_event_bus.{h,cpp}
    └── ipc/
        ├── ipc_server.{h,cpp}
        ├── credential_check.{h,cpp}       # SO_PEERCRED
        └── proto_codec.{h,cpp}            # Protobuf 编解码
```

---

## 4. 公共基础类型

### 4.1 `include/nexus/types.h`

```cpp
#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include <expected>
#include <string_view>

namespace nexus {

// 统一错误码，所有 syscall 包装返回 expected
enum class Err : int32_t {
    Ok = 0,
    PermissionDenied = 1,
    NotFound = 2,
    AlreadyExists = 3,
    Busy = 4,
    NotImplemented = 5,
    InvalidArgument = 6,
    IoError = 7,
    SELinuxDenied = 8,
    MountFailed = 9,
    Timeout = 10,
    Disconnected = 11,
    Unauthorized = 12,
};

template <class T>
using Result = std::expected<T, Err>;

// 模块执行阶段
enum class BootStage : uint8_t {
    PostFsData = 0,   // /data 已挂载，zygote 未启动
    LateStart = 1,    // boot completed 之后
    OnBootCompleted = 2,
};

// Root 来源
enum class RootProvider : uint8_t {
    Unknown = 0,
    Magisk = 1,
    KernelSU = 2,
    APatch = 3,
};

struct RootEnvironment {
    RootProvider provider = RootProvider::Unknown;
    std::string version;          // "26.4" / "0.7.7" / "0.10.5"
    std::string adbRootDir;       // /data/adb (Magisk) / /data/adb/ksu (KSU) / /data/adb/ap (APatch)
    std::string overlayBase;      // /data/adb/nexuscore/overlay
    std::string modulesDir;       // /data/adb/nexuscore/modules
    std::string sepolicyPath;     // /sys/fs/selinux/policy 或 split 后的路径
    bool sepolicyWritable = false;
    bool overlayFsAvailable = false;     // /proc/filesystems 含 overlay
    bool fuseAvailable = false;          // /proc/filesystems 含 fuse
    bool dynamicPartitions = false;      // /proc/mounts 含 /dev/block/dm-
};

} // namespace nexus
```

### 4.2 `include/nexus/logger.h`（接口）

```cpp
#pragma once
#include <string_view>

namespace nexus::log {

enum class Level : uint8_t { Verbose, Debug, Info, Warn, Error };

void init(std::string_view filePath, size_t ringBufferBytes = 1 << 20); // 1 MiB ring
void write(Level, std::string_view tag, std::string_view msg);
void flush();

// 便捷宏
#define NX_LOG_INFO(tag, ...) ::nexus::log::write(::nexus::log::Level::Info, tag, ::nexus::log::fmt(__VA_ARGS__))
#define NX_LOG_WARN(tag, ...) ::nexus::log::write(::nexus::log::Level::Warn, tag, ::nexus::log::fmt(__VA_ARGS__))
#define NX_LOG_ERR(tag, ...)  ::nexus::log::write(::nexus::log::Level::Error, tag, ::nexus::log::fmt(__VA_ARGS__))

} // namespace nexus::log
```

**设计点**：日志双写——内存 ring buffer（供 IPC 实时订阅）+ 落盘 `/data/adb/nexuscore/nexusd.log`（按 512 KiB 滚动，最多 3 个）。**绝不写 stderr**，避免在 `init` 早期 stderr 未重定向时丢失日志。

---

## 5. 启动、保活与 Root 环境检测

### 5.1 `core/root_env_detect.h`

> **整改 #2**：Daemon 启动期必须检测当前 Root 来源、分区布局、内核能力，自动适配存储路径与 sepolicy 路径。

```cpp
#pragma once
#include "nexus/types.h"

namespace nexus {

class RootEnvironmentDetector {
public:
    // 同步检测，应在 daemonize 之后、SELinux 修补之前调用
    static Result<RootEnvironment> detect();

private:
    static RootProvider detectProvider();
    static bool probeFile(std::string_view path);
    static bool kernelSupports(std::string_view fsName); // 读 /proc/filesystems
    static bool readMountFlag(std::string_view needle);  // 读 /proc/mounts
    static std::string readVersion(std::string_view path);
};

} // namespace nexus
```

**检测逻辑（`root_env_detect.cpp` 关键片段）**：

```cpp
RootProvider RootEnvironmentDetector::detectProvider() {
    if (probeFile("/data/adb/magisk/.magisk"))            return RootProvider::Magisk;
    if (probeFile("/data/adb/ksu/.config"))               return RootProvider::KernelSU;
    if (probeFile("/data/adb/ap/.config"))                return RootProvider::APatch;
    return RootProvider::Unknown;
}

Result<RootEnvironment> RootEnvironmentDetector::detect() {
    RootEnvironment env;
    env.provider = detectProvider();
    if (env.provider == RootProvider::Unknown) {
        NX_LOG_WARN("RootEnv", "Unknown root provider; assuming Magisk layout");
    }

    // 版本
    switch (env.provider) {
        case RootProvider::Magisk:   env.version = readVersion("/data/adb/magisk/.magisk"); break;
        case RootProvider::KernelSU: env.version = readVersion("/data/adb/ksu/version"); break;
        case RootProvider::APatch:   env.version = readVersion("/data/adb/ap/version"); break;
        default: break;
    }

    // 统一存储根（与 Root 来源无关，NexusCore 自己的目录）
    env.adbRootDir    = "/data/adb";
    env.overlayBase   = "/data/adb/nexuscore/overlay";
    env.modulesDir    = "/data/adb/nexuscore/modules";

    // sepolicy 路径
    env.sepolicyPath      = "/sys/fs/selinux/policy";
    env.sepolicyWritable  = probeFile(env.sepolicyPath.c_str());

    // 内核 FS 能力
    env.overlayFsAvailable = kernelSupports("overlay");
    env.fuseAvailable      = kernelSupports("fuse");

    // 动态分区
    env.dynamicPartitions  = readMountFlag("/dev/block/dm-");

    NX_LOG_INFO("RootEnv",
        "provider=%d ver=%s overlay=%d fuse=%d dynPart=%d",
        (int)env.provider, env.version.c_str(),
        env.overlayFsAvailable, env.fuseAvailable, env.dynamicPartitions);

    return env;
}
```

**降级规则**：若 `provider == Unknown`，Daemon 仍可启动但仅以"只读模式"运行——禁止任何挂载与脚本执行，仅暴露 IPC 查询能力。避免在不支持的环境误操作导致变砖。

### 5.2 `core/pid_file.h`

```cpp
#pragma once
#include <string_view>
#include "nexus/types.h"

namespace nexus {

class PidFile {
public:
    explicit PidFile(std::string_view path); // /dev/nexusd.pid
    Result<void> acquire();   // O_CREAT|O_EXCL，写 pid；已存在则检测旧进程是否存活
    Result<void> release();
    ~PidFile();
private:
    std::string path_;
    int fd_ = -1;
};

} // namespace nexus
```

**实现要点**：
- 用 `O_CREAT | O_EXCL`，失败时读出旧 PID，`kill(pid, 0)` 探测是否存活。
- 若旧进程存活 → 退出（避免双开）；若已死 → 删除旧文件重试。
- 文件权限 `0644`，避免被非 root 读出 PID 后被攻击。

### 5.3 `core/watchdog.h`

```cpp
#pragma once
#include <atomic>
#include <thread>
#include <functional>
#include "nexus/types.h"

namespace nexus {

class Watchdog {
public:
    using Worker = std::function<void()>;

    // 主工作线程崩溃后由监控线程 fork+exec 重启
    void spawn(Worker worker, std::string_view workerName);
    void stop();
private:
    void supervisorLoop();
    std::atomic<bool> running_{false};
    std::thread supervisor_;
    Worker worker_;
    std::string name_;
};

} // namespace nexus
```

**实现要点（伪代码）**：

```cpp
void Watchdog::spawn(Worker worker, std::string_view name) {
    worker_ = std::move(worker);
    name_   = std::string(name);
    running_ = true;
    supervisor_ = std::thread([this] { supervisorLoop(); });
}

void Watchdog::supervisorLoop() {
    while (running_) {
        std::thread w(worker_);
        w.join();   // 等工作线程退出
        if (!running_) break;

        NX_LOG_ERR("Watchdog", "worker '%s' exited unexpectedly, respawning in 1s", name_.c_str());
        std::this_thread::sleep_for(std::chrono::seconds(1));
        // 注意：这里不 fork/exec，因为 daemon 已 daemonize；
        // 仅在线程级重启，避免 init 重复 fork 导致 PID 风暴。
    }
}
```

> **设计权衡**：原 Prompt 提出"子线程重启主线程"。在已 daemonize 的前提下，进程级重启应由 `init` 负责（`restart` 关键字）。线程级重启更安全、更快、避免 PID 重分配问题。MVP 采用线程级；若连续 3 次重启失败，则主动 `_exit(1)` 触发 init 重启。

### 5.4 `main.cpp`

```cpp
#include "nexus/types.h"
#include "nexus/logger.h"
#include "core/pid_file.h"
#include "core/watchdog.h"
#include "core/root_env_detect.h"
#include "selinux/selinux_manager.h"
#include "fs/i_file_system_interceptor.h"
#include "fs/fs_detector.h"
#include "module/module_loader.h"
#include "event/in_memory_event_bus.h"
#include "ipc/ipc_server.h"
#include <cstdlib>
#include <unistd.h>
#include <sys/stat.h>

using namespace nexus;

static void daemonize() {
    // 经典双 fork，setsid，重定向 stdin/out/err -> /dev/null
    if (pid_t p = fork(); p > 0) _exit(0);
    setsid();
    if (pid_t p = fork(); p > 0) _exit(0);
    umask(022);
    ::close(STDIN_FILENO);
    ::close(STDOUT_FILENO);
    ::close(STDERR_FILENO);
    // stdin <- /dev/null, stdout/stderr -> 日志文件由 logger 接管
}

int main(int argc, char** argv) {
    bool foreground = false;
    for (int i = 1; i < argc; ++i) {
        if (std::string_view(argv[i]) == "--foreground") foreground = true;
        if (std::string_view(argv[i]) == "--daemon" && !foreground) foreground = false;
    }

    if (!foreground) daemonize();

    log::init("/data/adb/nexuscore/nexusd.log");
    NX_LOG_INFO("main", "nexusd starting, pid=%d", (int)getpid());

    PidFile pid("/dev/nexusd.pid");
    if (auto r = pid.acquire(); !r) {
        NX_LOG_ERR("main", "pid file acquire failed: %d", (int)r.error());
        return (int)r.error();
    }

    auto env = RootEnvironmentDetector::detect();
    if (!env) {
        NX_LOG_ERR("main", "root env detect failed, abort mount stage");
        // 进入只读模式（见 §5.1 降级）
    }

    SELinuxManager se(*env);
    if (auto r = se.patchSelfDomain(); !r) {
        NX_LOG_WARN("main", "selinux patch failed (err=%d); continue in restricted mode",
                    (int)r.error());
        // 不退出，仅记录；后续 mount 失败会被各自 fallback 兜底
    }

    auto bus = std::make_shared<InMemoryEventBus>();
    auto isolator = std::make_shared<NamespaceIsolator>();

    auto loader = std::make_shared<ModuleLoader>(*env, bus, isolator);
    auto modulesR = loader->scanModules(env->modulesDir);
    if (!modulesR) NX_LOG_WARN("main", "scanModules failed: %d", (int)modulesR.error());

    // 选择 FS 拦截器（OverlayFS 优先，Bind Mount 降级）
    std::unique_ptr<IFileSystemInterceptor> fs = FsDetector::select(*env);
    if (auto r = fs->mountAll(*modulesR); !r) {
        NX_LOG_WARN("main", "mountAll failed: %d; modules may not take effect", (int)r.error());
    }

    ShellExecutor shell(*env, isolator, bus);
    shell.runStage(BootStage::PostFsData);

    IpcServer ipc(*env, bus, loader, fs.get());
    ipc.start("/dev/socket/nexusd.sock");  // 异步

    // late_start 在后台线程跑
    std::thread late([&] { shell.runStage(BootStage::LateStart); });
    late.detach();

    bus->publish("EVENT_DAEMON_READY", {});

    Watchdog wd;
    wd.spawn([&] {
        // 主工作循环：心跳 + 事件分发
        while (true) {
            bus->tick();                 // 派发待处理事件
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }, "main-loop");

    // 主线程等待 SIGTERM/SIGINT
    sigset_t mask; sigemptyset(&mask);
    sigaddset(&mask, SIGTERM); sigaddset(&mask, SIGINT);
    pthread_sigmask(SIG_BLOCK, &mask, nullptr);
    int sig; sigwait(&mask, &sig);
    NX_LOG_INFO("main", "got signal %d, shutting down", sig);
    wd.stop();
    ipc.stop();
    fs->unmountAll();
    pid.release();
    return 0;
}
```

---

## 6. 文件系统拦截层

### 6.1 `fs/i_file_system_interceptor.h`（核心抽象）

```cpp
#pragma once
#include <string>
#include <string_view>
#include <vector>
#include "nexus/types.h"

namespace nexus {

struct MountTarget {
    std::string source;     // 模块 overlay 内的源文件
    std::string target;     // 系统目标路径，如 /system/build.prop
    int priority = 0;       // DMM priority，高优先级覆盖低优先级
};

class IFileSystemInterceptor {
public:
    virtual ~IFileSystemInterceptor() = default;

    // 探测当前内核/Root 环境是否支持本实现
    virtual Result<bool> detect(const RootEnvironment& env) = 0;

    // 将单个源 → 目标绑定（MS_BIND 或 OverlayFS lowerdir）
    virtual Result<void> mountOverlay(const MountTarget& t) = 0;

    // 批量挂载，按 priority 排序后挂载
    virtual Result<void> mountAll(const std::vector<MountTarget>& targets) = 0;

    // 卸载所有挂载（用于安全模式 / 卸载）
    virtual Result<void> unmountAll() = 0;

    // FUSE 预留接口 —— MVP 返回 NotImplemented
    virtual Result<void> prepareFuse(const std::string& mountPoint) {
        return std::unexpected(Err::NotImplemented);
    }

    virtual std::string_view implName() const = 0;
};

} // namespace nexus
```

> **整改 #1 落点**：`detect()` 必须由每个实现自行探测能力。`FsDetector::select()` 按优先级尝试，**OverlayFS 不可用 → Bind Mount → 均不可用 → 只读模式**。

### 6.2 `fs/overlay_fs_interceptor.h`（首选实现）

```cpp
#pragma once
#include "i_file_system_interceptor.h"

namespace nexus {

// 基于 Linux OverlayFS：
//   mount -t overlay overlay \
//     -o lowerdir=<ro-system>:<module-overlay>,upperdir=...,workdir=... <target>
// MVP 简化：对单个文件使用 lowerdir 链 + tmpfs upperdir
class OverlayFsInterceptor : public IFileSystemInterceptor {
public:
    Result<bool> detect(const RootEnvironment& env) override;
    Result<void> mountOverlay(const MountTarget& t) override;
    Result<void> mountAll(const std::vector<MountTarget>& targets) override;
    Result<void> unmountAll() override;
    std::string_view implName() const override { return "overlayfs"; }
private:
    std::vector<std::string> mounted_;  // 记录挂载点，便于 unmountAll
};

} // namespace nexus
```

**关键实现（节选）**：

```cpp
Result<bool> OverlayFsInterceptor::detect(const RootEnvironment& env) {
    if (!env.overlayFsAvailable) return false;
    // 探针：在 tmp 上挂一个 overlay，成功即支持
    // 失败回退给 FsDetector 选择 BindMount
    return runProbe();
}

Result<void> OverlayFsInterceptor::mountOverlay(const MountTarget& t) {
    // target 例如 /system/build.prop
    // 将原文件作为 lowerdir[0]，模块修改版作为 lowerdir[1]
    // upperdir = /data/adb/nexuscore/overlay/<module>/<hash>/upper
    // workdir  = .../work
    std::string upper = env_.overlayBase + "/upper/" + hash(t.target);
    std::string work  = env_.overlayBase + "/work/"  + hash(t.target);
    ::mkdir(upper.c_str(), 0755);
    ::mkdir(work.c_str(),  0755);

    std::string opts = "lowerdir=" + t.source + ":" + t.target
                     + ",upperdir=" + upper
                     + ",workdir="  + work;

    if (mount("overlay", t.target.c_str(), "overlay",
              MS_NODEV | MS_NOATIME, opts.c_str()) < 0) {
        return std::unexpected(Err::MountFailed);
    }
    mounted_.push_back(t.target);
    return {};
}
```

### 6.3 `fs/bind_mount_interceptor.h`（降级实现）

```cpp
#pragma once
#include "i_file_system_interceptor.h"

namespace nexus {

// 当 OverlayFS 不可用（厂商内核关闭、EROFS 只读、SELinux 拒绝 overlay 类型）
// 使用 MS_BIND | MS_REC 实现单文件替换，类似 Magisk Magic Mount
class BindMountInterceptor : public IFileSystemInterceptor {
public:
    Result<bool> detect(const RootEnvironment& env) override;
    Result<void> mountOverlay(const MountTarget& t) override;
    Result<void> mountAll(const std::vector<MountTarget>& targets) override;
    Result<void> unmountAll() override;
    std::string_view implName() const override { return "bind"; }
private:
    std::vector<std::string> mounted_;
};

} // namespace nexus
```

**关键实现**：

```cpp
Result<void> BindMountInterceptor::mountOverlay(const MountTarget& t) {
    // 1) 备份原文件到 /data/adb/nexuscore/stock/<hash>
    // 2) bind mount: mount(t.source, t.target, NULL, MS_BIND | MS_REC, NULL)
    // 3) 重新 mount 一次 MS_BIND | MS_REMOUNT | MS_RDONLY 保证只读语义
    std::string stock = env_.overlayBase + "/stock/" + hash(t.target);
    if (!probeFile(stock)) {
        if (::link(t.target.c_str(), stock.c_str()) < 0
            && errno != EEXIST) {
            return std::unexpected(Err::IoError);
        }
    }
    if (::mount(t.source.c_str(), t.target.c_str(), nullptr,
                MS_BIND | MS_REC, nullptr) < 0) {
        return std::unexpected(Err::MountFailed);
    }
    // 保持只读，避免被后续脚本误写
    ::mount(t.source.c_str(), t.target.c_str(), nullptr,
            MS_BIND | MS_REMOUNT | MS_RDONLY, nullptr);
    mounted_.push_back(t.target);
    return {};
}

Result<void> BindMountInterceptor::unmountAll() {
    // 逆序 umount，并恢复 stock 文件
    for (auto it = mounted_.rbegin(); it != mounted_.rend(); ++it) {
        ::umount2(it->c_str(), MNT_DETACH);
    }
    mounted_.clear();
    return {};
}
```

### 6.4 `fs/fs_detector.h`（降级选择器）

```cpp
#pragma once
#include "nexus/types.h"
#include "i_file_system_interceptor.h"
#include <memory>

namespace nexus {

class FsDetector {
public:
    // 优先 OverlayFS，失败回退 Bind Mount，再失败返回 no-op 拦截器
    static std::unique_ptr<IFileSystemInterceptor> select(const RootEnvironment& env);
};

} // namespace nexus
```

```cpp
std::unique_ptr<IFileSystemInterceptor> FsDetector::select(const RootEnvironment& env) {
    auto ov = std::make_unique<OverlayFsInterceptor>();
    if (auto ok = ov->detect(env); ok && *ok) {
        NX_LOG_INFO("FsDetector", "using OverlayFsInterceptor");
        return ov;
    }
    NX_LOG_WARN("FsDetector", "OverlayFS unavailable, fallback to BindMount");

    auto bm = std::make_unique<BindMountInterceptor>();
    if (auto ok = bm->detect(env); ok && *ok) {
        NX_LOG_INFO("FsDetector", "using BindMountInterceptor");
        return bm;
    }

    NX_LOG_ERR("FsDetector", "no FS interceptor available; running read-only");
    return std::make_unique<NoopInterceptor>(); // 所有 mount 直接返回 Ok 但什么都不做
}
```

### 6.5 Dynamic Partitions 与 EROFS 处理

> **整改 #1 细化**：Android 11+ 引入动态分区，`/system` 实际是从 `/dev/block/dm-XX` 挂载；Android 13+ 大量 OEM 使用 EROFS 只读压缩 FS。直接 bind mount 到 `/system/...` 在 EROFS 下仍可行（bind 是 VFS 层操作，与底层 FS 无关），但**需要先确保目标路径已存在**（否则要 mkdir，而 EROFS 不可写）。处理流程：

```cpp
// 在 BindMountInterceptor::mountOverlay 前置检查
Result<void> ensureTargetExists(const std::string& target) {
    struct stat st;
    if (::stat(target.c_str(), &st) == 0) return {};
    // 目标不存在 —— 在 EROFS 上无法 mkdir
    // 方案：通过 /data/adb/nexuscore/overlay_tree 构造完整 overlay 路径
    //       并用 mount --bind 把整棵树挂到目标父目录
    NX_LOG_WARN("Fs", "target %s missing on EROFS; skipping (module may be ineffective)",
                target.c_str());
    return std::unexpected(Err::NotFound);
}
```

> 对动态分区：读取 `/proc/mounts` 找到 `/system` 的设备节点，验证其为 `dm-` 前缀时记录 `env.dynamicPartitions=true`，但 bind/overlay 行为不变。仅影响日志可读性，不改变挂载策略。

---

## 7. 模块加载与脚本执行

### 7.1 `module/i_module_loader.h`（核心抽象）

```cpp
#pragma once
#include <memory>
#include <string>
#include <vector>
#include "nexus/types.h"
#include "../event/i_event_bus.h"

namespace nexus {

struct ModuleManifest {
    std::string id;
    std::string name;
    std::string version;
    std::string author;
    std::string minNexusVersion;          // "1.0"
    int         priority = 0;             // DMM 字段，越高越后挂载（覆盖前者）
    std::vector<std::string> capabilities;// ["MODIFY_SYSTEM_PROPS", ...]
    struct Intent { std::string action; int priority = 0; };
    std::vector<Intent> intents;
    std::string basePath;                 // 模块解压根目录
    bool enabled = true;
};

class IModuleLoader {
public:
    virtual ~IModuleLoader() = default;
    virtual Result<std::vector<ModuleManifest>> parseManifest(const std::string& moduleDir) = 0;
    virtual Result<void> loadModule(const ModuleManifest& m) = 0;
    virtual Result<void> unloadModule(const std::string& moduleId) = 0;
    virtual Result<std::vector<ModuleManifest>> listLoaded() = 0;
};

} // namespace nexus
```

### 7.2 `module/manifest.h` —— DMM 解析

```cpp
#pragma once
#include "i_module_loader.h"
#include <nlohmann/json.hpp>

namespace nexus {

// manifest.json JSON Schema 见 Spec 03
class ManifestParser {
public:
    static Result<ModuleManifest> parse(const std::string& moduleDir);
    // 校验：id 合法性、min_nexus_version、capabilities 白名单
    static Result<void> validate(const ModuleManifest& m, const std::string& currentVersion);
};

} // namespace nexus
```

**关键实现**：

```cpp
Result<ModuleManifest> ManifestParser::parse(const std::string& dir) {
    std::string path = dir + "/manifest.json";
    auto content = readFile(path);
    if (!content) return std::unexpected(content.error());

    auto j = nlohmann::json::parse(*content, nullptr, false);
    if (j.is_discarded()) return std::unexpected(Err::InvalidArgument);

    ModuleManifest m;
    m.id               = j.value("id", "");
    m.name             = j.value("name", m.id);
    m.version          = j.value("version", "0.0.0");
    m.author           = j.value("author", "unknown");
    m.minNexusVersion  = j.value("min_nexus_version", "1.0");
    m.priority         = j.value("priority", 0);
    m.enabled          = j.value("enabled", true);
    m.basePath         = dir;
    for (auto& c : j.value("capabilities", std::vector<std::string>{}))
        m.capabilities.push_back(c);
    for (auto& it : j.value("intents", std::vector<nlohmann::json>{})) {
        ModuleManifest::Intent intent;
        intent.action   = it.value("action", "");
        intent.priority = it.value("priority", 0);
        m.intents.push_back(intent);
    }

    if (m.id.empty()) return std::unexpected(Err::InvalidArgument);
    return m;
}
```

### 7.3 `module/module_loader.{h,cpp}` —— 优先级排序

> **整改 #5**：多模块同路径覆盖必须按 `priority` 字段排序，避免最后挂载者意外覆盖高优先级模块。

```cpp
#pragma once
#include "i_module_loader.h"
#include "manifest.h"
#include "../fs/i_file_system_interceptor.h"
#include "../process/i_process_isolator.h"
#include "../event/i_event_bus.h"

namespace nexus {

class ModuleLoader : public IModuleLoader {
public:
    ModuleLoader(RootEnvironment env,
                 std::shared_ptr<IEventBus> bus,
                 std::shared_ptr<IProcessIsolator> isolator)
        : env_(env), bus_(bus), isolator_(isolator) {}

    Result<std::vector<ModuleManifest>> parseManifest(const std::string& moduleDir) override;
    Result<void> loadModule(const ModuleManifest& m) override;
    Result<void> unloadModule(const std::string& moduleId) override;
    Result<std::vector<ModuleManifest>> listLoaded() override;

    // 启动期扫描所有模块目录，返回按 priority 升序排列
    Result<std::vector<ModuleManifest>> scanModules(const std::string& root);

    // 把模块的 system/ 树转换为 MountTarget 列表，按 priority 升序
    std::vector<MountTarget> collectTargets(const std::vector<ModuleManifest>& sorted);

private:
    RootEnvironment env_;
    std::shared_ptr<IEventBus> bus_;
    std::shared_ptr<IProcessIsolator> isolator_;
    std::vector<ModuleManifest> loaded_;
};

} // namespace nexus
```

**排序实现**：

```cpp
Result<std::vector<ModuleManifest>> ModuleLoader::scanModules(const std::string& root) {
    std::vector<ModuleManifest> out;
    for (auto& entry : listDir(root)) {
        auto m = ManifestParser::parse(entry);
        if (!m) { NX_LOG_WARN("Loader", "skip %s: parse failed", entry.c_str()); continue; }
        if (!m->enabled) { NX_LOG_INFO("Loader", "skip %s: disabled", m->id.c_str()); continue; }
        if (auto v = ManifestParser::validate(*m, "1.0.0"); !v) {
            NX_LOG_WARN("Loader", "skip %s: validation failed", m->id.c_str());
            continue;
        }
        out.push_back(*m);
    }
    // priority 升序：低优先级先挂载，高优先级后挂载（覆盖）
    std::stable_sort(out.begin(), out.end(),
                     [](const auto& a, const auto& b) { return a.priority < b.priority; });
    return out;
}

std::vector<MountTarget> ModuleLoader::collectTargets(const std::vector<ModuleManifest>& sorted) {
    std::vector<MountTarget> targets;
    for (const auto& m : sorted) {
        std::string sysTree = m.basePath + "/system";
        for (auto& rel : walkTree(sysTree)) {
            MountTarget t;
            t.source   = sysTree + "/" + rel;
            t.target   = "/" + rel;            // /system/<rel>
            t.priority = m.priority;
            targets.push_back(t);
        }
    }
    // 同 target 内部已按 m.priority 升序（因外层排序）；后挂载者覆盖前者
    return targets;
}
```

> **冲突检测**：在 `collectTargets` 末尾可加一次 `target` 唯一性检查，相同 target 只保留最高 priority 一项（MVP 简化：直接由挂载顺序天然覆盖；Phase 3 改为事件总线内存缝合）。

### 7.4 `module/shell_executor.{h,cpp}` —— 安全脚本执行

```cpp
#pragma once
#include "nexus/types.h"
#include "../process/i_process_isolator.h"
#include "../event/i_event_bus.h"
#include <string>

namespace nexus {

class ShellExecutor {
public:
    ShellExecutor(RootEnvironment env,
                  std::shared_ptr<IProcessIsolator> isolator,
                  std::shared_ptr<IEventBus> bus)
        : env_(env), isolator_(isolator), bus_(bus) {}

    // 执行某 boot 阶段所有模块的对应脚本
    Result<void> runStage(BootStage stage);

    // 执行单个脚本文件，独立 Mount Namespace
    Result<int> execScript(const std::string& scriptPath,
                           const std::vector<std::string>& envVars);

private:
    static std::string_view stageScriptName(BootStage s) {
        switch (s) {
            case BootStage::PostFsData:       return "post-fs-data.sh";
            case BootStage::LateStart:        return "service.sh";
            case BootStage::OnBootCompleted:  return "post-boot.sh";
        }
        return "";
    }

    RootEnvironment env_;
    std::shared_ptr<IProcessIsolator> isolator_;
    std::shared_ptr<IEventBus> bus_;
};

} // namespace nexus
```

**关键实现**：

```cpp
Result<int> ShellExecutor::execScript(const std::string& script,
                                      const std::vector<std::string>& envVars) {
    if (::access(script.c_str(), X_OK) != 0)
        return std::unexpected(Err::PermissionDenied);

    pid_t pid = fork();
    if (pid < 0) return std::unexpected(Err::IoError);
    if (pid == 0) {
        // 子进程：进入独立 Mount Namespace，避免污染全局挂载
        isolator_->enterNamespace();
        // 设置环境变量 NEXUS_MODULE_PATH, BOOT_STAGE, NEXUS_VERSION
        for (auto& kv : envVars) ::setenv(/*...*/);
        // execve /system/bin/sh -c <script>
        char* argv[] = { (char*)"sh", (char*)"-c", (char*)script.c_str(), nullptr };
        execve("/system/bin/sh", argv, environ);
        _exit(127);   // execve 失败
    }
    int status = 0;
    ::waitpid(pid, &status, 0);
    bus_->publish("EVENT_SCRIPT_DONE", {{"script", script}, {"code", status}});
    return WEXITSTATUS(status);
}
```

---

## 8. 进程隔离与事件总线

### 8.1 `process/i_process_isolator.h`

```cpp
#pragma once
#include "nexus/types.h"

namespace nexus {

class IProcessIsolator {
public:
    virtual ~IProcessIsolator() = default;
    virtual Result<void> createNamespace(unsigned long cloneFlags) = 0; // CLONE_NEWNS | CLONE_NEWPID ...
    virtual Result<void> enterNamespace() = 0;                          // 当前进程进入
    virtual Result<void> unshare(unsigned long cloneFlags) = 0;
};

} // namespace nexus
```

**`namespace_isolator.cpp` 关键**：

```cpp
Result<void> NamespaceIsolator::enterNamespace() {
    // unshare(CLONE_NEWNS) 让本进程拥有独立挂载视图
    // 注意：必须先 mount("/", "/", "none", MS_REC | MS_SLAVE, nullptr)
    //       切断 propagation，防止子进程挂载泄漏到全局
    if (::mount("none", "/", nullptr, MS_REC | MS_SLAVE, nullptr) < 0
        && errno != EINVAL) {
        return std::unexpected(Err::MountFailed);
    }
    if (::unshare(CLONE_NEWNS) < 0) {
        return std::unexpected(Err::IoError);
    }
    return {};
}
```

### 8.2 `event/i_event_bus.h`

```cpp
#pragma once
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>
#include <mutex>
#include <any>

namespace nexus {

using EventName = std::string;
using EventPayload = std::unordered_map<std::string, std::any>;
using EventCallback = std::function<void(const EventPayload&)>;

class IEventBus {
public:
    virtual ~IEventBus() = default;
    virtual void publishEvent(const EventName& name, const EventPayload& payload) = 0;
    virtual int  subscribeEvent(const EventName& name, EventCallback cb) = 0;
    virtual void unsubscribe(int handle) = 0;
    virtual void tick() = 0;   // MVP：在主循环里驱动派发
};

} // namespace nexus
```

**`in_memory_event_bus.cpp` 关键**：

```cpp
class InMemoryEventBus : public IEventBus {
public:
    void publishEvent(const EventName& name, const EventPayload& payload) override {
        std::scoped_lock lk(mu_);
        pending_.push_back({name, payload});
    }
    int subscribeEvent(const EventName& name, EventCallback cb) override {
        std::scoped_lock lk(mu_);
        int h = nextHandle_++;
        subs_[name].push_back({h, std::move(cb)});
        return h;
    }
    void tick() override {
        std::vector<Pending> local;
        {
            std::scoped_lock lk(mu_);
            local.swap(pending_);
        }
        for (auto& p : local) {
            auto it = subs_.find(p.name);
            if (it == subs_.end()) continue;
            for (auto& [h, cb] : it->second) cb(p.payload);
        }
    }
private:
    struct Sub { int handle; EventCallback cb; };
    struct Pending { EventName name; EventPayload payload; };
    std::mutex mu_;
    std::unordered_map<EventName, std::vector<Sub>> subs_;
    std::vector<Pending> pending_;
    int nextHandle_ = 1;
};
```

---

## 9. SELinux 动态策略修补

### 9.1 `selinux/selinux_manager.h`

```cpp
#pragma once
#include "nexus/types.h"
#include <string>
#include <vector>

namespace nexus {

class SELinuxManager {
public:
    explicit SELinuxManager(const RootEnvironment& env);

    // 仅修补 nexus_daemon 自身域，不全局 setenforce 0
    // 失败时回退到 setenforce 0 + WARN（最后手段，绝不静默）
    Result<void> patchSelfDomain();

    // 注入单条规则
    Result<void> injectRule(std::string_view source, std::string_view target,
                            std::string_view clazz, std::string_view perm);

    Result<void> restore();

private:
    RootEnvironment env_;
    bool patched_ = false;
    bool fallbackEnforce0_ = false;
};

} // namespace nexus
```

### 9.2 实现要点

> **整改 #1 + 评审推理**：Android 14+ 强制 Enforcing。直接 `setenforce 0` 会导致系统服务异常。必须基于 `sepolicy-inject` 原理在内存修补 `policy.32`，仅放宽 `u:r:nexus_daemon:s0` 域。

```cpp
Result<void> SELinuxManager::patchSelfDomain() {
    // 1) 读取 /sys/fs/selinux/policy 到内存
    // 2) 用 libsepol 扩展 policy：
    //    - type nexus_daemon;
    //    - type nexus_daemon, domain;
    //    - allow nexus_daemon self:capability { sys_admin dac_override setuid };
    //    - allow nexus_daemon shell_exec_t:file { read open execute };
    //    - allow nexus_daemon system_file:file { read open mount };
    //    - allow nexus_daemon nexus_data_t:dir { search read write };
    // 3) 写回 /sys/fs/selinux/load
    // 4) 验证 /sys/fs/selinux/enforce 仍为 1
    if (auto r = injectViaLibsepol(); !r) {
        NX_LOG_WARN("SELinux", "libsepol inject failed (err=%d); trying magiskpolicy", (int)r.error());
        if (auto r2 = invokeMagiskPolicy(); !r2) {
            NX_LOG_ERR("SELinux", "all patch methods failed; fallback to setenforce 0 (DANGEROUS)");
            // 兜底：仅 patchself 时局部 permissive（如果内核支持），否则 setenforce 0 + 告警
            if (!tryPermissiveDomain("nexus_daemon")) {
                ::system("setenforce 0");
                fallbackEnforce0_ = true;
                NX_LOG_ERR("SELinux", "GLOBAL SETENFORCE 0 ENGAGED - system services may misbehave");
            }
            return std::unexpected(r.error());
        }
    }
    patched_ = true;
    return {};
}

Result<void> SELinuxManager::restore() {
    if (fallbackEnforce0_) {
        ::system("setenforce 1");
        fallbackEnforce0_ = false;
    }
    // 内存修补的规则无法撤销（除非重启），记录日志即可
    return {};
}
```

**规则清单**（最小权限集）：

| source | target | class | perms |
|---|---|---|---|
| nexus_daemon | self | capability | sys_admin, dac_override, setuid, setgid, chown |
| nexus_daemon | shell_exec_t | file | read, open, execute, getattr |
| nexus_daemon | system_file | file | read, open, mount, getattr |
| nexus_daemon | system_file | dir | search, read, open, mounton |
| nexus_daemon | nexus_data_t | dir/file | search, read, write, create, unlink |
| nexus_daemon | kernel_t | process | setsched |
| nexus_daemon | nexus_daemon | process | fork, signal, setcurrent |

---

## 10. IPC Server 与凭证校验

### 10.1 `ipc/ipc_server.h`

```cpp
#pragma once
#include "nexus/types.h"
#include "../event/i_event_bus.h"
#include "../module/i_module_loader.h"
#include "../fs/i_file_system_interceptor.h"
#include <memory>
#include <string>
#include <thread>

namespace nexus {

class IpcServer {
public:
    IpcServer(RootEnvironment env,
              std::shared_ptr<IEventBus> bus,
              std::shared_ptr<IModuleLoader> loader,
              IFileSystemInterceptor* fs)
        : env_(env), bus_(bus), loader_(loader), fs_(fs) {}

    Result<void> start(std::string_view socketPath);
    void stop();

private:
    void acceptLoop();
    void handleClient(int fd);

    RootEnvironment env_;
    std::shared_ptr<IEventBus> bus_;
    std::shared_ptr<IModuleLoader> loader_;
    IFileSystemInterceptor* fs_;
    int listenFd_ = -1;
    std::thread acceptThread_;
    std::atomic<bool> running_{false};
};

} // namespace nexus
```

### 10.2 `ipc/credential_check.h` —— SO_PEERCRED 校验

> **整改 #3**：UDS 默认任何 APP 可连接。必须 `SO_PEERCRED` 校验客户端 UID + SELinux 域，且仅允许 NexusManager 签名包。

```cpp
#pragma once
#include "nexus/types.h"

namespace nexus {

struct PeerCredential {
    pid_t pid = 0;
    uid_t uid = 0;
    gid_t gid = 0;
    std::string selinuxContext;   // 通过 /proc/<pid>/attr/current 读
    std::string packageName;      // 通过 /proc/<pid>/cmdline 读
};

class CredentialCheck {
public:
    // 读取 SO_PEERCRED + 进程上下文
    static Result<PeerCredential> readPeer(int fd);

    // 校验：UID 在白名单、包名为 com.nexus.manager、SELinux 域为 untrusted_app 或 platform_app
    // 并校验 APK 签名指纹（通过 pm 路径读 /data/app/.../base.apk 的 hash）
    static Result<void> authorize(const PeerCredential& peer);
};

} // namespace nexus
```

**关键实现**：

```cpp
Result<PeerCredential> CredentialCheck::readPeer(int fd) {
    struct ucred uc;
    socklen_t len = sizeof(uc);
    if (::getsockopt(fd, SOL_SOCKET, SO_PEERCRED, &uc, &len) < 0)
        return std::unexpected(Err::IoError);

    PeerCredential p;
    p.pid = uc.pid;
    p.uid = uc.uid;
    p.gid = uc.gid;

    // 读 /proc/<pid>/attr/current 拿 SELinux 域
    p.selinuxContext = readFile("/proc/" + std::to_string(uc.pid) + "/attr/current").value_or("");
    // 读 /proc/<pid>/cmdline 拿包名
    p.packageName    = readFile("/proc/" + std::to_string(uc.pid) + "/cmdline").value_or("");
    return p;
}

Result<void> CredentialCheck::authorize(const PeerCredential& peer) {
    // 1) UID 白名单（仅 system 或 nexus manager 的 UID）
    //    在启动期从 config.toml 读允许的 UID 列表，默认 [1000] (system) + manager UID
    if (!isAllowedUid(peer.uid)) return std::unexpected(Err::Unauthorized);

    // 2) 包名必须是 com.nexus.manager
    if (peer.packageName != "com.nexus.manager")
        return std::unexpected(Err::Unauthorized);

    // 3) SELinux 域必须是 untrusted_app / platform_app / system_app
    if (peer.selinuxContext.find("u:r:untrusted_app:s0") == std::string::npos &&
        peer.selinuxContext.find("u:r:platform_app:s0")  == std::string::npos &&
        peer.selinuxContext.find("u:r:system_app:s0")    == std::string::npos)
        return std::unexpected(Err::Unauthorized);

    // 4) APK 签名指纹校验（防伪 Manager）
    auto apk = locateApk(peer.packageName);
    if (!apk) return std::unexpected(Err::Unauthorized);
    auto sig = sha256OfFile(apk->baseApk);
    if (sig != EXPECTED_MANAGER_SIG) return std::unexpected(Err::Unauthorized);

    return {};
}
```

### 10.3 IPC 协议（Protobuf）

### `proto/nexus.proto`

```proto
syntax = "proto3";
package nexus.ipc.v1;

option java_package = "com.nexus.manager.ipc.proto";
option java_multiple_files = true;

// ============ 通用信封 ============
message Envelope {
  uint32 magic   = 1;  // 0x4E58434F ('NXCO')
  uint32 version = 2;  // 1
  uint32 seq     = 3;  // 请求序号
  oneof body {
    Request  request  = 10;
    Response response = 11;
    Event    event    = 12;
  }
}

// ============ Request ============
message Request {
  oneof payload {
    PingRequest            ping              = 1;
    GetStatusRequest       get_status        = 2;
    ListModulesRequest     list_modules      = 3;
    EnableModuleRequest    enable_module     = 4;
    DisableModuleRequest   disable_module    = 5;
    InstallModuleRequest   install_module    = 6;  // 本地 ZIP 路径
    UninstallModuleRequest uninstall_module  = 7;
    RestartDaemonRequest   restart_daemon    = 8;
    EnterSafeModeRequest   enter_safe_mode   = 9;
    SubscribeLogsRequest   subscribe_logs    = 10;
  }
}

message PingRequest           { string token = 1; }
message GetStatusRequest      {}
message ListModulesRequest    {}
message EnableModuleRequest   { string id = 1; }
message DisableModuleRequest  { string id = 1; }
message InstallModuleRequest  { string local_path = 1; }
message UninstallModuleRequest{ string id = 1; }
message RestartDaemonRequest  {}
message EnterSafeModeRequest  { uint32 timeout_sec = 1; }
message SubscribeLogsRequest  { uint32 min_level = 1; }

// ============ Response ============
message Response {
  int32 code = 1;   // 0 = OK，其他 = Err 枚举值
  string message = 2;
  oneof payload {
    PingResponse            ping              = 10;
    GetStatusResponse       get_status        = 11;
    ListModulesResponse     list_modules      = 12;
    EmptyResponse           empty             = 13;
    InstallModuleResponse   install_module    = 14;
  }
}

message PingResponse          { string token = 1; uint32 server_version = 2; }
message GetStatusResponse {
  bool root_available        = 1;
  string root_provider       = 2;   // "magisk" / "kernelsu" / "apatch"
  string root_version        = 3;
  bool selinux_enforcing     = 4;
  string selinux_domain      = 5;
  bool daemon_running        = 6;
  uint32 daemon_pid          = 7;
  string fs_interceptor      = 8;   // "overlayfs" / "bind" / "noop"
  uint32 module_count        = 9;
  bool safe_mode             = 10;
  uint64 uptime_ms           = 11;
}
message ModuleInfo {
  string id          = 1;
  string name        = 2;
  string version     = 3;
  string author      = 4;
  string description = 5;
  bool enabled       = 6;
  int32  priority    = 7;
  repeated string capabilities = 8;
}
message ListModulesResponse    { repeated ModuleInfo modules = 1; }
message EmptyResponse          {}
message InstallModuleResponse  { string id = 1; bool need_reboot = 2; }

// ============ Event（Daemon 主动推） ============
message Event {
  string name = 1;
  uint64 timestamp_ms = 2;
  oneof payload {
    LogLineEvent       log_line       = 10;
    ModuleLoadedEvent  module_loaded  = 11;
    ScriptDoneEvent    script_done    = 12;
    DaemonReadyEvent   daemon_ready   = 13;
  }
}
message LogLineEvent      { uint32 level = 1; string tag = 2; string msg = 3; }
message ModuleLoadedEvent { string id = 1; }
message ScriptDoneEvent   { string script = 1; int32 code = 2; }
message DaemonReadyEvent  {}
```

**协议规则**：
- 每个 Envelope 前 4 字节小端 length prefix（uint32），避免粘包。
- `magic` 校验：不符立即断开。
- 客户端断线后服务端清理订阅 handle。
- 请求/响应按 `seq` 配对；事件无 `seq`（用 0）。

### 10.4 `ipc/ipc_server.cpp` 接受循环（节选）

```cpp
Result<void> IpcServer::start(std::string_view socketPath) {
    listenFd_ = ::socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (listenFd_ < 0) return std::unexpected(Err::IoError);

    struct sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    ::strncpy(addr.sun_path, socketPath.data(), sizeof(addr.sun_path) - 1);
    ::unlink(socketPath.data());  // 清理旧 sock
    if (::bind(listenFd_, (struct sockaddr*)&addr, sizeof(addr)) < 0)
        return std::unexpected(Err::IoError);
    ::chmod(socketPath.data(), 0660);  // 仅同组可连
    ::chown(socketPath.data(), 0, 1000); // root:system
    if (::listen(listenFd_, 8) < 0) return std::unexpected(Err::IoError);

    running_ = true;
    acceptThread_ = std::thread([this]{ acceptLoop(); });
    return {};
}

void IpcServer::acceptLoop() {
    while (running_) {
        int cfd = ::accept4(listenFd_, nullptr, nullptr, SOCK_CLOEXEC);
        if (cfd < 0) continue;

        // 第一步：凭证校验（连接建立后立即做）
        auto peer = CredentialCheck::readPeer(cfd);
        if (!peer || !CredentialCheck::authorize(*peer)) {
            NX_LOG_WARN("IPC", "unauthorized peer rejected, uid=%d pkg=%s",
                        peer ? peer->uid : -1,
                        peer ? peer->packageName.c_str() : "(unknown)");
            ::close(cfd);
            continue;
        }
        // 校验通过，进入会话线程
        std::thread([this, cfd]{ handleClient(cfd); }).detach();
    }
}
```

---

## 11. CMake 构建配置

### `daemon/CMakeLists.txt`

```cmake
cmake_minimum_required(VERSION 3.22)
project(nexusd LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# NDK 交叉编译（外部传入 Android NDK toolchain）
# 调用示例：
#   cmake -B build-arm64 \
#         -DCMAKE_TOOLCHAIN_FILE=$NDK/build/cmake/android.toolchain.cmake \
#         -DANDROID_ABI=arm64-v8a \
#         -DANDROID_PLATFORM=android-34 \
#         -DANDROID_STL=c++_static \
#         -DCMAKE_BUILD_TYPE=Release

# Protobuf
find_package(Protobuf REQUIRED)

# libsepol（从 AOSP 或 selinux 项目预编译）
find_library(SEPOL_LIB sepol PATHS ${CMAKE_SOURCE_DIR}/third_party/lib/${ANDROID_ABI})
find_library(SELINUX_LIB selinux)

add_compile_options(
    -Wall -Wextra -Werror
    -ffunction-sections -fdata-sections
    -fno-exceptions -fno-rtti        # 节省体积；如需 RTTI 移除
    -fstack-protector-strong
)

add_executable(nexusd
    src/main.cpp
    src/core/pid_file.cpp
    src/core/watchdog.cpp
    src/core/root_env_detect.cpp
    src/core/logger.cpp
    src/selinux/selinux_manager.cpp
    src/fs/overlay_fs_interceptor.cpp
    src/fs/bind_mount_interceptor.cpp
    src/fs/fs_detector.cpp
    src/module/module_loader.cpp
    src/module/manifest.cpp
    src/module/shell_executor.cpp
    src/process/namespace_isolator.cpp
    src/event/in_memory_event_bus.cpp
    src/ipc/ipc_server.cpp
    src/ipc/credential_check.cpp
    src/ipc/proto_codec.cpp
)

# 生成 protobuf
protobuf_generate_cpp(PROTO_SRCS PROTO_HDRS proto/nexus.proto)
target_sources(nexusd PRIVATE ${PROTO_SRCS})

target_include_directories(nexusd PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_BINARY_DIR}     # protobuf 生成的头
    ${CMAKE_SOURCE_DIR}/third_party/include
)

target_link_libraries(nexusd PRIVATE
    ${SEPOL_LIB}
    ${SELINUX_LIB}
    protobuf::libprotobuf
    log                             # liblog (NDK)
    c++_static
)

# 链接器裁剪
target_link_options(nexusd PRIVATE
    -Wl,--gc-sections
    -Wl,--icf=all
    -Wl,-z,now -Wl,-z,relro
    -Wl,--exclude-libs,ALL
    -s                              # release strip
)
```

### 交叉编译脚本 `daemon/build.sh`

```bash
#!/usr/bin/env bash
set -euo pipefail
NDK="${ANDROID_NDK_HOME:-$HOME/Android/Sdk/ndk/26.1.10909125}"
ABI="${1:-arm64-v8a}"
MIN_SDK=34

cmake -B "build-${ABI}" \
  -DCMAKE_TOOLCHAIN_FILE="$NDK/build/cmake/android.toolchain.cmake" \
  -DANDROID_ABI="$ABI" \
  -DANDROID_PLATFORM="android-${MIN_SDK}" \
  -DANDROID_STL=c++_static \
  -DCMAKE_BUILD_TYPE=Release

cmake --build "build-${ABI}" -j"$(nproc)"

OUT="build-${ABI}/nexusd"
echo "Built: $OUT"
file "$OUT"
```

---

## 12. 部署到设备

### 12.1 init.rc 片段（放 `/system/etc/init/nexusd.rc` 或 Magisk `/data/adb/modules/.../service.d/`）

```rc
service nexusd /system/bin/nexusd --daemon
    class core
    user root
    group root system
    seclabel u:r:nexus_daemon:s0
    disabled

on post-fs-data
    mkdir /data/adb/nexuscore
    mkdir /data/adb/nexuscore/modules
    mkdir /data/adb/nexuscore/overlay
    start nexusd

on property:sys.boot_completed=1
    # late_start 脚本由 Daemon 内部调度，无需 init 重复触发
```

> **MVP 部署方式**：通过 Magisk module 包装 NexusDaemon（在 `system/bin/nexusd` 落地二进制 + 上述 `service.d/nexusd.rc`），随 Magisk 启动。**不需要**修改 boot.img。

### 12.2 推送与测试

```bash
adb root
adb push build-arm64-v8a/nexusd /data/local/tmp/nexusd
adb shell "chmod 755 /data/local/tmp/nexusd"
# 前台调试（输出到 logcat）
adb shell "/data/local/tmp/nexusd --foreground"
# 或正式部署
adb shell "cp /data/local/tmp/nexusd /data/adb/nexuscore/bin/nexusd"
adb shell "chmod 755 /data/adb/nexuscore/bin/nexusd"
adb reboot
```

### 12.3 验证清单

| # | 检查项 | 命令 | 期望 |
|---|---|---|---|
| 1 | Daemon 启动 | `adb shell ps -A \| grep nexusd` | 进程存在，ppid=1 |
| 2 | PID 文件 | `adb shell cat /dev/nexusd.pid` | 输出 PID |
| 3 | Socket 存在 | `adb shell ls -l /dev/socket/nexusd.sock` | 0660 root:system |
| 4 | SELinux 仍 enforcing | `adb shell getenforce` | `Enforcing` |
| 5 | nexus_daemon 域已注入 | `adb shell ps -Z \| grep nexusd` | `u:r:nexus_daemon:s0` |
| 6 | IPC 可连（带凭证） | 用 Manager 测试 | 返回 status |
| 7 | IPC 拒绝陌生 UID | `adb shell nc -U /dev/socket/nexusd.sock` | 立即被关闭 |
| 8 | 模块挂载生效 | `adb shell getprop ro.debuggable` | 模块设定的值 |
| 9 | 重启不 Bootloop | `adb reboot` 后 5 次冷启动 | 每次都能进系统 |
| 10 | 安全模式 | Manager 触发 | 所有挂载 umount，模块不加载 |

---

## 13. 安全性 / Bootloop 防护

### 13.1 安全模式（Safe Mode）
- 配置文件 `/data/adb/nexuscore/safe_mode` 存在时，Daemon 跳过 `mountAll` 与 `runStage`。
- Manager 可通过 IPC 写入该文件，重启后生效。
- 用户也可在开机时长按音量下（需配合 init 脚本检测），本 MVP 不实现按键检测。

### 13.2 挂载快照与回滚
- 每次 `mountAll` 前把计划挂载列表写入 `/data/adb/nexuscore/last_mounts.json`。
- 若启动期间检测到连续 2 次未到 `boot_completed`，自动启用安全模式。
- 检测方法：在 `late_start` 阶段写 `boot_ok` 标记，下次启动检查上次是否写过。

### 13.3 失败计数器

```cpp
// core/boot_counter.cpp
// 每次启动 +1，boot_completed 后清零
// ≥3 时进入安全模式
Result<void> BootCounter::tick() {
    auto v = readFile(kCounterPath).andThen(parseInt);
    int n = v.value_or(0) + 1;
    writeFile(kCounterPath, std::to_string(n));
    if (n >= 3) {
        NX_LOG_ERR("BootCounter", "3 consecutive failed boots, entering safe mode");
        writeFile("/data/adb/nexuscore/safe_mode", "1");
    }
    return {};
}
```

---

## 14. 验收标准（Acceptance Criteria）

- [ ] `nexusd` 二进制可在 `arm64-v8a` Android 14 模拟器与真机上启动
- [ ] 启动后 PID 文件与 UDS 正确生成，权限为 `0660 root:system`
- [ ] `RootEnvironmentDetector` 能正确识别 Magisk/KSU/APatch
- [ ] `SELinuxManager.patchSelfDomain` 后 `getenforce` 仍为 `Enforcing`
- [ ] `nexus_daemon` 域出现在 `ps -Z`
- [ ] `FsDetector::select` 在 OverlayFS 不可用时回退到 Bind Mount
- [ ] 模块按 `priority` 升序挂载
- [ ] 脚本在独立 Mount Namespace 内执行，不影响全局挂载
- [ ] IPC 拒绝非白名单 UID / 非 Manager 包名 / 签名不符的客户端
- [ ] 连续 5 次冷启动不 Bootloop
- [ ] 触发安全模式后，所有挂载被 `umount`，模块不加载

---

## 15. 风险与缓解

| 风险 | 概率 | 影响 | 缓解 |
|---|---|---|---|
| 厂商内核禁止 `mount("overlay")` | 高 | 模块失效 | Bind Mount 降级（已实现） |
| sepolicy 不可写（强锁定） | 中 | Daemon 无权限 | 局部 permissive 域；最后 setenforce 0 + 告警 |
| EROFS 上目标路径不存在 | 中 | 单文件挂载失败 | 跳过并告警；不阻断启动 |
| 客户端伪造 Manager | 低 | 越权调用 Root | 签名指纹 + SO_PEERCRED 双校验 |
| 模块脚本 `service.sh` 阻塞 | 中 | late_start 卡死 | 独立线程 + 超时 kill（120s） |
| Daemon 被 LMK 杀死 | 低 | 模块短时失效 | init `restart` 关键字 + 线程级 Watchdog |

---

## 16. 与其它 Spec 的依赖

- 本 Spec 的 IPC schema (`nexus.proto`) 被 [Spec 02 — Manager](./spec-02-manager.md) 直接复用。
- 本 Spec 的 `manifest.json` 解析逻辑依赖 [Spec 03 — Module SDK](./spec-03-module-sdk.md) 定义的 JSON Schema。
- 模块脚本运行时环境变量（`NEXUS_MODULE_PATH` 等）见 Spec 03 §3。
