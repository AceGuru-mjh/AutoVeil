# NexusCore Spec 缜密加固与 Bug 修复 Spec

| 字段 | 值 |
|---|---|
| Change ID | `harden-spec-and-fix-bugs` |
| 版本 | 1.0.0 |
| 状态 | Proposed |
| 适用阶段 | Bug Fix + Spec Sync |
| 触发原因 | 代码评审发现 7 个🔴严重 bug、3 个🟠 spec/impl 背离、5 个🟡设计疑点 |

---

## Why

代码与 spec 评审发现：[spec-01-daemon.md](file:///workspace/nexuscore/specs/spec-01-daemon.md) 中 Daemon 设计存在多处会在落地时直接崩溃或永远失败的逻辑错误（OverlayFS 用文件当 lowerdir、bind mount 跨 fs 硬链接、SO_PEERCRED context 匹配错误、socket 权限让合法 Manager 进不来、main.cpp 解引用失败 Result 触发 UB）；[spec-02-manager.md](file:///workspace/nexuscore/specs/spec-02-manager.md) 已严重落后于实际实现（Spec 声明 3 页面且 SU 是非目标，但实际已实现 SuperUser/Settings/ModuleDetail 共 6 页面，并新增 Reboot/UninstallFramework/SuPolicy 等 RPC）；示例模块 [customize.sh](file:///workspace/nexuscore/modules/nexus_prop_editor/customize.sh) 调用未定义的 `abort` 函数；定位与命名（AutoVeil / NexusCore / Android Root Framework）混乱且与实际能力（寄生在 Magisk/KSU/APatch 之上、不提供 root）不符。这些问题在写代码前修掉成本最低。

---

## What Changes

### Daemon Spec（spec-01）技术 bug 修复
- **修复 OverlayFS lowerdir 用法**：禁止把文件路径作为 lowerdir；替换单文件必须构造「目标父目录 + 同名文件」的目录树，或对目录级别 overlay
- **修复 Bind Mount 备份方式**：禁止使用 `link(2)`（跨 fs 返回 EXDEV）；改为 `read + write` 复制原文件到 stock 目录
- **修复 SO_PEERCRED SELinux context 匹配**：匹配规则从精确子串 `"u:r:untrusted_app:s0"` 改为前缀 `u:r:untrusted_app`，兼容 Android 10+ 的 `untrusted_app_30:s0:c512,c768` 格式
- **修复 Socket 权限与凭据校验的 catch-22**：socket 不能既 0660 root:system 又指望 untrusted_app 能连。引入「Magisk libsu 风格 root-side 桥接」或允许 0660 root:shell + Manager 通过 `su` 间接连
- **修复 main.cpp 解引用失败 Result 的 UB**：`detect()` 失败时显式进入只读模式（赋默认 `RootEnvironment`）或 `return`，绝不继续 `*env`
- **修复 `sepolicyWritable` 命名与语义**：从 `probeFile` 改为 `::access(path, W_OK) == 0`，真正检测可写
- **补充 USERSPACE 重启实现路径**：明确 `RebootRequest.Mode.USERSPACE` 通过 `setprop sys.powerctl=userspace`，由底层 root（Magisk/KSU/APatch）已注入的 SELinux 规则放行

### Manager Spec（spec-02）与实现同步
- **更新页面清单**：从「3 页面」改为「6 页面」——Dashboard / Modules / ModuleDetail / SuperUser / Logs / Settings
- **新增 Navigation 依赖说明**：使用 Navigation Compose 而非简单 when 分支
- **新增 Biometric / DataStore / WorkManager 依赖说明**
- **新增 RPC 清单**：`ListSuApps` / `SetSuPolicy` / `ListSuLogs` / `ClearLogs` / `Reboot` / `UninstallFramework` 全部补入 Spec
- **明确 SU 管理的产品定位**：要么删除「Root UI 是非目标」声明并解释为何要做自己的 SU（代理底层 root 的策略层？还是与底层冲突？），要么删除 SuperUser 相关代码与 proto 字段
- **删除 `ModuleInfo.has_update`/`update_url` 或更新 Spec**：Spec 明确「自动检查更新」是非目标，但 proto 已加字段，二选一

### 示例模块修复
- **修复 customize.sh `abort` 未定义**：补 shim `abort() { echo "$1" >&2; exit 1; }`
- **示例模块目标重新评估**：`/system/build.prop` 在 Android 14+ 经常只剩占位（真实属性散落到 prop.default / vendor / product），要么改示例目标为 `/system/etc/hosts`，要么在 README 明确限制
- **补全 Magisk 兼容 shim**：`SKIPUNZIP` 真实语义、其它常用 Magisk 函数（如 `set_perm`/`set_perm_recursive` 已有但语义需校对）

### 定位与命名统一
- **重定位**：从「Android 用户态 Root 框架」改为「寄生在 Magisk/KSU/APatch 之上的用户态模块运行时（userspace module runtime）」——明确不提供 root、不提供 SU 授权（除非产品决策要做）
- **统一命名**：AutoVeil / NexusCore / Android Root Framework 三选一，全仓库统一
- **修正虚假宣传**：[web/index.html](file:///workspace/nexuscore/web/index.html) 把 Phase 1 标为「已完成 90%+」与实际状态（Daemon 未实现）不符，改为「Daemon 实现中」

### 文档修复
- **修正 Mount Namespace 误解**：[developer-guide.md](file:///workspace/nexuscore/sdk/docs/developer-guide.md) 「service.sh 在独立 Mount NS，写入会随进程退出丢失」是错的——Mount NS 只隔离 mount 操作，不隔离对已挂载 fs（如 /data）的写操作。`/data/adb/nexuscore/<id>/runtime.log` 会正常持久化
- **模块格式决策**：明确「完全兼容 Magisk module.prop」还是「坚持 DMM，提供 Magisk 兼容层」。若选 DMM，需强化 capabilities 强制校验（当前 `WRITE_DATA_DIR`/`NETWORK_ACCESS` 仅「记录」，无强制力，难以吸引开发者迁移）

---

## Impact

### 受影响的 Spec 文档
- [spec-01-daemon.md](file:///workspace/nexuscore/specs/spec-01-daemon.md) — §5.1（sepolicyWritable）、§5.4（main.cpp UB）、§6.2（OverlayFS）、§6.3（Bind Mount link）、§10.2（SO_PEERCRED）、§10.4（Socket 权限）、新增 §10.5（USERSPACE 重启）
- [spec-02-manager.md](file:///workspace/nexuscore/specs/spec-02-manager.md) — §1.1（页面数）、§1.2（SU 非目标）、§2（目录结构）、§8（依赖）、§10（RPC 清单）
- [spec-03-module-sdk.md](file:///workspace/nexuscore/specs/spec-03-module-sdk.md) — §6.4（customize.sh shim）、模块格式决策

### 受影响的代码
- [nexuscore/daemon/](file:///workspace/nexuscore/daemon/) — 整个目录（尚未实现，spec 修完后才动手）
- [nexuscore/modules/nexus_prop_editor/customize.sh](file:///workspace/nexuscore/modules/nexus_prop_editor/customize.sh) — `abort` shim
- [nexuscore/modules/nexus_prop_editor/manifest.json](file:///workspace/nexuscore/modules/nexus_prop_editor/manifest.json) — `homepage` 字段
- [nexuscore/manager/app/src/main/proto/nexus.proto](file:///workspace/nexuscore/manager/app/src/main/proto/nexus.proto) — `ModuleInfo.has_update`/`update_url` 取舍
- [nexuscore/README.md](file:///workspace/nexuscore/README.md) — 定位描述
- [nexuscore/web/index.html](file:///workspace/nexuscore/web/index.html) — Phase 1 状态、定位标语
- [nexuscore/sdk/docs/developer-guide.md](file:///workspace/nexuscore/sdk/docs/developer-guide.md) — Mount NS 误解
- [README.md](file:///workspace/README.md) — AutoVeil/NexusCore 命名

---

## ADDED Requirements

### Requirement: Daemon 设计必须可通过编译期检查避免 UB

Spec 01 中所有 `Result<T>` 解引用场景必须显式处理失败分支，禁止「注释说降级、代码继续 deref」的模式。

#### Scenario: detect() 失败时
- **WHEN** `RootEnvironmentDetector::detect()` 返回 `!env`
- **THEN** main.cpp 必须 `return` 退出，或显式构造默认 `RootEnvironment{provider=Unknown}` 进入只读模式
- **AND** 绝不继续执行 `SELinuxManager se(*env)`

#### Scenario: 任何 syscall 包装返回失败时
- **WHEN** `mount()` / `link()` / `access()` 等返回 < 0
- **THEN** 必须记录 errno 与失败路径，并显式返回 `std::unexpected(Err::xxx)`
- **AND** 禁止以「fallback 在注释里说明」替代代码层 fallback

### Requirement: SELinux context 匹配必须兼容 Android 10+ 命名

`CredentialCheck::authorize` 中的 SELinux 域匹配必须使用**前缀匹配**而非精确子串匹配，以兼容 `untrusted_app_25` ~ `untrusted_app_32` 等带版本后缀的 context。

#### Scenario: 合法 Manager（Android 14）
- **WHEN** peer 的 selinuxContext 为 `u:r:untrusted_app_30:s0:c512,c768,...`
- **THEN** 必须通过校验（匹配前缀 `u:r:untrusted_app`）
- **AND** 不应被 `find("u:r:untrusted_app:s0") == npos` 误拒

### Requirement: Socket 权限必须让合法 Manager 能 connect

UDS 服务端 socket 的权限与属主必须与客户端（untrusted_app）的实际 gid 匹配，或采用 root-side 桥接方案。

#### Scenario: 使用直接 UDS
- **WHEN** Daemon 选择「Manager 直连 UDS」方案
- **THEN** socket 权限必须为 `0666` 或 `0660 root:<manager_gid>`，且 Manager 的 gid 必须真实可加入
- **AND** 文档必须说明 Android 14+ untrusted_app 默认不在 system 组（gid 1000）

#### Scenario: 使用 root-side 桥接
- **WHEN** Daemon 选择「Manager 经底层 root 间接连」方案（Magisk libsu 风格）
- **THEN** 必须文档化桥接路径：Manager → `su` → Daemon socket
- **AND** SO_PEERCRED 此时拿到的是 root 或 shell uid，校验规则需相应调整

### Requirement: Bind Mount 备份禁止跨 fs 硬链接

`BindMountInterceptor::mountOverlay` 中备份原文件必须使用 `read + write` 复制，禁止使用 `link(2)`。

#### Scenario: 备份 /system/build.prop
- **WHEN** 目标 `/system/build.prop` 位于 EROFS/squashfs，stock 目录 `/data/adb/...` 位于 ext4/f2fs
- **THEN** `link()` 会返回 `EXDEV` 永远失败
- **AND** 必须改用 `open(src, O_RDONLY) → read → open(stock, O_WRONLY|O_CREAT) → write` 复制

### Requirement: OverlayFS lowerdir 必须是目录

`OverlayFsInterceptor::mountOverlay` 中所有 `lowerdir` / `upperdir` / `workdir` 必须是目录路径，禁止传文件路径。

#### Scenario: 替换单个文件 /system/build.prop
- **WHEN** 目标是单个文件 `/system/build.prop`
- **THEN** 必须构造目录树：lowerdir = `/tmp/nexus_lower_<hash>/system`（内含同名 build.prop 文件），挂载点 = `/system`（目录级别）
- **AND** 禁止 `lowerdir=/path/to/build.prop:/system/build.prop` 这种把文件当目录的写法

#### Scenario: OverlayFS 不可用
- **WHEN** 内核不支持 overlay 或厂商禁用
- **THEN** FsDetector 必须自动降级到 Bind Mount
- **AND** 再不可用进入只读模式，绝不崩溃

### Requirement: customize.sh 必须提供完整 Magisk 兼容 shim

NexusCore 的 customize.sh 执行环境必须 shim 所有 Magisk 通用函数，否则会因 `command not found` 而静默失败。

#### Scenario: 模块调用 abort
- **WHEN** customize.sh 调用 `abort "reason"`
- **THEN** NexusCore 必须已定义 `abort() { echo "$1" >&2; exit 1; }`
- **AND** 退出码为 1，Daemon 记录安装失败并清理临时目录

#### Scenario: 模块调用 SKIPUNZIP
- **WHEN** customize.sh 设置 `SKIPUNZIP=1`
- **THEN** Daemon 必须跳过自动解压，由脚本自行处理
- **AND** 必须文档化此变量的真实语义

### Requirement: Spec 与实现必须双向同步

任何 spec 中声明的非目标都不允许在代码中悄悄实现；任何代码中已实现的功能都必须在 spec 中有对应描述。

#### Scenario: 新增 RPC
- **WHEN** Manager 端新增 `RebootRequest` / `UninstallFrameworkRequest` / `SetSuPolicyRequest` 等 RPC
- **THEN** 必须同步更新 spec-02 的 RPC 清单与产品定位说明
- **AND** 若与原非目标冲突，必须显式修订非目标条款并说明决策理由

#### Scenario: 页面数变化
- **WHEN** 实际页面数从 3 增加到 6
- **THEN** spec-02 §1.1 必须更新页面清单
- **AND** 必须为每个新增页面补充设计章节

### Requirement: 项目定位描述必须与实际能力一致

README、web、spec 中对 NexusCore 的定位描述必须明确「不提供 root、寄生在 Magisk/KSU/APatch 之上」。

#### Scenario: 用户阅读 README
- **WHEN** 用户首次访问仓库
- **THEN** README 第一段必须明确：「NexusCore 是寄生在 Magisk/KernelSU/APatch 之上的用户态模块运行时，自身不提供 root」
- **AND** 不应使用「Android Root 框架」这种易与 Magisk 混淆的措辞

### Requirement: 进度声明必须与实际状态一致

文档中的「已完成」「90%+」等进度声明必须可验证。

#### Scenario: Web 路线图状态
- **WHEN** Phase 1 的 Daemon 尚未实现
- **THEN** web/index.html 的 Phase 1 状态必须为「Daemon 实现中」或「设计完成，编码未开始」
- **AND** 禁止标「已完成 90%+」

---

## MODIFIED Requirements

### Requirement: RootEnvironment.sepolicyWritable

修改 [spec-01 §4.1](file:///workspace/nexuscore/specs/spec-01-daemon.md) 与 §5.1：

- 字段语义：从「`probeFile` 检测存在」改为「`::access(path, W_OK) == 0` 检测可写」
- 默认值：`false`
- 实现：`env.sepolicyWritable = (::access(env.sepolicyPath.c_str(), W_OK) == 0);`

### Requirement: CredentialCheck.authorize

修改 [spec-01 §10.2](file:///workspace/nexuscore/specs/spec-01-daemon.md)：

SELinux 域匹配规则从精确子串改为前缀匹配：
```cpp
auto startsWith = [](std::string_view s, std::string_view prefix) {
    return s.substr(0, prefix.size()) == prefix;
};
bool okContext = startsWith(peer.selinuxContext, "u:r:untrusted_app")
              || startsWith(peer.selinuxContext, "u:r:platform_app")
              || startsWith(peer.selinuxContext, "u:r:system_app");
```

### Requirement: BindMountInterceptor.mountOverlay

修改 [spec-01 §6.3](file:///workspace/nexuscore/specs/spec-01-daemon.md)：

备份方式从 `link(2)` 改为 `read + write`：
```cpp
// 旧（错误）：跨 fs 返回 EXDEV
// if (::link(t.target.c_str(), stock.c_str()) < 0 && errno != EEXIST) { ... }

// 新（正确）：复制
if (!probeFile(stock)) {
    if (!copyFile(t.target, stock)) {
        return std::unexpected(Err::IoError);
    }
}
```

### Requirement: OverlayFsInterceptor.mountOverlay

修改 [spec-01 §6.2](file:///workspace/nexuscore/specs/spec-01-daemon.md)：

替换单文件时构造目录树：
```cpp
// 1) 创建 lower 目录树：/tmp/nexus_lower_<hash>/system/build.prop
std::string lowerRoot = "/tmp/nexus_lower_" + hash(t.target);
std::string lowerDir  = lowerRoot + parentDirOf(t.target);  // /system
::mkdir_p(lowerDir);
::copyFile(t.target, lowerRoot + t.target);  // 把原文件放进 lower

// 2) 创建模块 source 目录树（同样的结构）
std::string modDir = env_.overlayBase + "/lower_<modhash>" + parentDirOf(t.target);
::mkdir_p(modDir);
::copyFile(t.source, modDir + basenameOf(t.target));

// 3) overlay 挂载到目标的父目录（目录级别，非文件）
std::string opts = "lowerdir=" + modDir + ":" + lowerDir
                 + ",upperdir=" + upper
                 + ",workdir="  + work;
::mount("overlay", parentDirOf(t.target).c_str(), "overlay", MS_NODEV|MS_NOATIME, opts.c_str());
```

### Requirement: main.cpp 启动失败处理

修改 [spec-01 §5.4](file:///workspace/nexuscore/specs/spec-01-daemon.md)：

```cpp
auto envR = RootEnvironmentDetector::detect();
RootEnvironment env;
if (!envR) {
    NX_LOG_ERR("main", "root env detect failed; entering read-only mode");
    env = RootEnvironment{};  // 默认值，provider=Unknown
    // 跳过 mount 阶段，仅启动 IPC 查询
} else {
    env = *envR;
}
```

### Requirement: Spec 02 页面清单

修改 [spec-02 §1.1](file:///workspace/nexuscore/specs/spec-02-manager.md)：

从「3 个核心 Compose 页面：Dashboard、Modules、Logs」改为：

「6 个 Compose 页面：Dashboard、Modules、ModuleDetail、SuperUser、Logs、Settings。使用 Navigation Compose 管理路由，底部导航栏显示 5 个顶级页面，ModuleDetail 为详情页（无底栏）。」

### Requirement: Spec 02 RPC 清单

修改 [spec-02](file:///workspace/nexuscore/specs/spec-02-manager.md) 新增章节「§12 完整 RPC 清单」：

补入：`Ping` / `GetStatus` / `ListModules` / `EnableModule` / `DisableModule` / `InstallModule` / `UninstallModule` / `RestartDaemon` / `EnterSafeMode` / `SubscribeLogs` / `ListSuApps` / `SetSuPolicy` / `ListSuLogs` / `ClearLogs` / `Reboot` / `UninstallFramework`，每条说明调用者、权限要求、返回值。

### Requirement: developer-guide.md Mount NS 说明

修改 [developer-guide.md §10](file:///workspace/nexuscore/sdk/docs/developer-guide.md)：

删除「service.sh 在独立 Mount NS，写入会随进程退出丢失」的误导性表述，改为：

「service.sh 在独立 Mount Namespace 内执行，**仅隔离 mount/unmount 操作**，不隔离对已挂载 fs（如 /data）的写操作。`/data/adb/nexuscore/<id>/` 下的写入会正常持久化。若需写入临时且不持久的数据，使用 `$NEXUS_TMPDIR`（位于 tmpfs，进程退出即清）。」

### Requirement: nexus.proto ModuleInfo 字段

修改 [nexus.proto](file:///workspace/nexuscore/manager/app/src/main/proto/nexus.proto)：

二选一：
- **方案 A（删除）**：移除 `has_update` 与 `update_url` 字段，与 spec-02 §1.2「模块自升级、自动检查更新是非目标」一致
- **方案 B（保留+更新 spec）**：保留字段，更新 spec-02 §1.2 移除该非目标声明，并补充「MVP 仅显示字段，不实现检查逻辑；Phase 2 引入 updateJson 检查」

### Requirement: customize.sh shim 补全

修改 [modules/nexus_prop_editor/customize.sh](file:///workspace/nexuscore/modules/nexus_prop_editor/customize.sh) 与 spec-03 §6.4：

补 shim：
```sh
ui_print() { echo "$1"; }
abort() { echo "!" "$1" >&2; exit 1; }
set_perm() { chmod "$3" "$1"; }
set_perm_recursive() {
    dir="$1"; own="$2"; grp="$3"; dirperm="$4"; fileperm="$5"
    find "$dir" -type d -exec chmod "$dirperm" {} \; 2>/dev/null
    find "$dir" -type f -exec chmod "$fileperm" {} \; 2>/dev/null
}
```

---

## REMOVED Requirements

### Requirement: Spec 02 §1.2「Root 授权管理 UI 是非目标」

**Reason**：与实际实现冲突。Manager 已实现 SuperUserPage / SuRequestDialog / SuRequestViewModel 与 `SetSuPolicy` / `ListSuApps` / `SuRequestEvent` 等 RPC，明确在做 SU 管理。

**Migration**：
- 若产品决策「要做自己的 SU」：删除该非目标条款，新增 §13「SuperUser 管理」章节，说明 NexusCore 的 SU 是「代理底层 root（Magisk/KSU/APatch）的策略层」，与底层 Manager 是协作而非竞争关系，且需说明当底层 root 已有自己的 SU（如 MagiskSU）时如何避免双重弹窗
- 若产品决策「不做 SU」：删除 SuperUserPage / SuRequestDialog / SuRequestViewModel 与 proto 中所有 Su 相关 RPC、`SuAppInfo` / `SuLogEntry` / `SuRequestEvent` 等消息

**决策需在 spec 阶段完成，二选一**。

### Requirement: 「Android 用户态 Root 框架」定位措辞

**Reason**：NexusCore 不提供 root、不提供 SU（除非上述决策选「要做」），实际是寄生在 Magisk/KSU/APatch 之上的模块运行时。称「Root 框架」易与 Magisk 混淆，引发「为什么不直接用 Magisk」质疑。

**Migration**：全仓库统一改为「Userspace Module Runtime on top of Magisk/KernelSU/APatch」（中文：「基于 Magisk/KernelSU/APatch 的用户态模块运行时」）。
