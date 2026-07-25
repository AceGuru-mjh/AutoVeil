# AutoVeil / NexusCore

> **建立在 Magisk / KernelSU / APatch 之上的用户态模块运行时（userspace module runtime）**
>
> ⚠️ **重要定位**：NexusCore **不提供 root，不提供 su**。它必须安装在有 Magisk / KSU / APatch
> 的设备上，作为这些底层 root 框架的"模块管理与运行时层"。如果你只想要 root，请直接用
> Magisk / KSU / APatch；如果你想做模块开发，希望有声明式清单、capabilities 强制校验、
> 独立 mount namespace 沙盒、统一 Manager UI，NexusCore 是更现代化的选择。

## 状态

| 组件 | 状态 | 说明 |
|---|---|---|
| `specs/` 设计文档 | ✅ 已完成 | 3 份 spec，约 16 万字，覆盖 daemon / manager / module SDK |
| `manager/` Android 客户端 | ✅ UI 完整 | Jetpack Compose，6 个页面，IPC + MVVM |
| `daemon/` C++ Root 守护进程 | 🚧 MVP 骨架 | 详见 [daemon/README.md](nexuscore/daemon/README.md) |
| `modules/` 示例模块 | ✅ 2 个 | `nexus_prop_editor`（build.prop 演示）、`nexus_hosts_editor`（推荐使用） |
| `web/` 知识图谱 | ✅ 已完成 | [web/index.html](nexuscore/web/index.html) |
| 测试 | 🚧 占位 | daemon 单元测试骨架，manager 端尚无测试 |
| CI/CD | 🚧 未配置 | 计划加入 GitHub Actions 编译 manager APK + 校验 daemon C++ |

## 与主流 Root 方案的关系

NexusCore **不是** Magisk / KSU / APatch 的替代品，而是它们的**模块运行时层**：

```
┌─────────────────────────────────────────┐
│  Magisk / KSU / APatch (底层 root)       │
│  - 提供 root 进程能力（CAP_SYS_ADMIN 等）│
│  - 提供 SELinux 策略注入工具              │
│  - 启动 nexusd（作为 Magisk 模块的 daemon）│
└─────────────────────────────────────────┘
                  ▼ fork & exec
┌─────────────────────────────────────────┐
│  nexusd (NexusCore Daemon, 用户态)        │
│  - 接管模块挂载、脚本执行、SU 策略持久化   │
│  - 所有 root 能力通过底层 root 授权       │
│  - 不与底层 root 的 su 冲突（共存模式）   │
└─────────────────────────────────────────┘
                  ▼ UDS IPC
┌─────────────────────────────────────────┐
│  NexusManager (Android Compose App)       │
│  - Dashboard / Modules / SuperUser /      │
│    Logs / Settings / ModuleDetail         │
└─────────────────────────────────────────┘
```

详见 [Spec 01 §14](nexuscore/specs/spec-01-daemon.md#14-补充章节与底层-root-的关系定位)。

## 核心特性

- **声明式模块清单（DMM）**：JSON `manifest.json` 替代 Magisk 的 `module.prop`，支持 capabilities、intents、priority 等结构化字段
- **Capabilities 强制校验**：未声明的能力一律拒绝执行，比 Magisk 的"脚本可任意作为"更安全
- **独立 Mount Namespace 脚本沙盒**：每个模块的 `post-fs-data.sh` / `service.sh` 在独立 NS 内执行
- **四层严格分离**：Manager 客户端绝不直接执行 root 命令，所有 root 操作经 IPC → Daemon
- **Magisk 兼容 shim**：注入 `ui_print` / `set_perm` / `set_perm_recursive` / `abort` / `MODPATH` 等，降低迁移成本
- **OverlayFS + Bind Mount 双引擎**：自动探测内核能力，EROFS/动态分区下自动降级
- **零 Bootloop 设计**：任何 syscall 失败必须有 fallback，绝不导致系统无法启动

## 目录结构

```
AutoVeil/
├── README.md                              # 本文件
└── nexuscore/
    ├── daemon/                            # C++ Root 守护进程 (nexusd)
    │   ├── CMakeLists.txt
    │   ├── include/nexus/                 # 头文件
    │   ├── src/                           # 实现源文件
    │   ├── proto/nexus.proto              # 与 manager 共享的 IPC schema
    │   ├── scripts/                       # 安装脚本（Magisk 模块包装）
    │   ├── tests/                         # 单元测试
    │   └── README.md
    ├── manager/                           # Kotlin/Compose 客户端
    │   ├── app/
    │   │   ├── build.gradle.kts
    │   │   └── src/main/
    │   │       ├── AndroidManifest.xml
    │   │       ├── java/com/nexus/manager/
    │   │       └── proto/nexus.proto      # 与 daemon 共享
    │   └── README.md
    ├── modules/                           # 示例模块
    │   ├── nexus_prop_editor/             # build.prop 演示（Android 14+ 上不可靠）
    │   └── nexus_hosts_editor/            # hosts 编辑器（推荐使用）
    ├── sdk/                               # 模块开发者 SDK
    │   ├── nexus_module.schema.json       # manifest.json JSON Schema
    │   └── docs/developer-guide.md
    ├── specs/                             # 开发规约文档
    │   ├── spec-01-daemon.md              # Daemon 核心架构
    │   ├── spec-02-manager.md             # NexusManager
    │   └── spec-03-module-sdk.md          # Module SDK
    └── web/
        └── index.html                     # 知识图谱站点
```

## 快速开始

### 1. 安装底层 Root（前置条件）

任选其一：[Magisk](https://github.com/topjohnwu/Magisk) / [KernelSU](https://github.com/tiann/KernelSU) / [APatch](https://github.com/bmax121/APatch)。

### 2. 编译 NexusManager APK

```bash
cd nexuscore/manager
./gradlew :app:assembleRelease
# 产出：app/build/outputs/apk/release/app-release.apk
adb install app/build/outputs/apk/release/app-release.apk
```

### 3. 编译并部署 nexusd

```bash
# 需要 NDK r26d+
export NDK=/path/to/android-ndk
cd nexuscore/daemon
cmake -B build-arm64 \
    -DCMAKE_TOOLCHAIN_FILE=$NDK/build/cmake/android.toolchain.cmake \
    -DANDROID_ABI=arm64-v8a \
    -DANDROID_PLATFORM=android-34 \
    -DANDROID_STL=c++_static
cmake --build build-arm64 -j

# 推送到设备（通过 Magisk 模块包装，详见 daemon/scripts/customize.sh）
adb push build-arm64/nexusd /data/local/tmp/nexusd
adb shell "su -c 'mkdir -p /data/adb/nexuscore/bin && cp /data/local/tmp/nexusd /data/adb/nexuscore/bin/nexusd && chmod 755 /data/adb/nexuscore/bin/nexusd'"
adb reboot
```

### 4. 安装示例模块

```bash
cd nexuscore/modules/nexus_hosts_editor
./build.sh
# 产出 dist/nexus_nexus_hosts_editor_1.0.0.zip
# 在 NexusManager 中"从本地 ZIP 安装"
```

## 开发规约

- [Spec 01 — Daemon 核心架构](nexuscore/specs/spec-01-daemon.md)
- [Spec 02 — NexusManager](nexuscore/specs/spec-02-manager.md)
- [Spec 03 — Module SDK](nexuscore/specs/spec-03-module-sdk.md)
- [开发者指南](nexuscore/sdk/docs/developer-guide.md)

## 设计原则

1. 仅用户态 syscall，绝不编写内核模块 (.ko)
2. 任何 syscall 失败必须有 fallback，绝不导致 Bootloop
3. 客户端绝不直接执行 Root 命令，所有 Root 操作经 IPC → Daemon
4. 声明式模块清单 (DMM)，未声明的能力一律拒绝

## 目标系统

Android 14 / 15 / 16 (API 34+), arm64-v8a

## 命名说明

仓库 `AutoVeil` 是项目代号（最初定位自动化与隐私增强），内部代号 `NexusCore`。
GitHub 仓库保留 `AutoVeil` 不变（避免历史链接失效），但所有文档与代码内部统一使用
`NexusCore` 作为产品名。如果你看到任何 `AutoVeil` 与 `NexusCore` 混用，请理解它们是
同一个项目的不同代号。

## 贡献

详见 [CONTRIBUTING.md](CONTRIBUTING.md)（待补）。当前最需要的贡献方向：
- daemon RPC handlers 真实实现（当前为占位）
- 单元测试覆盖（daemon 端 + manager 端）
- CI/CD pipeline
- Magisk 模块仓库索引（Phase 2）
- Zygisk 等价物（Phase 3）

## License

待定（建议 Apache 2.0 或 MIT）。
