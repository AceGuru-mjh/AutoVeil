<div align="center">

# 🛡️ NexusCore

### 建立在 Magisk / KernelSU / APatch 之上的用户态模块运行时

**Android 14+ · Rust + C++ + Kotlin · 声明式 · 进程隔离 · 零 Bootloop**

[![Build Manager APK](https://github.com/AceGuru-mjh/AutoVeil/actions/workflows/manager.yml/badge.svg)](https://github.com/AceGuru-mjh/AutoVeil/actions/workflows/manager.yml)
[![Build & Test Daemon](https://github.com/AceGuru-mjh/AutoVeil/actions/workflows/daemon.yml/badge.svg)](https://github.com/AceGuru-mjh/AutoVeil/actions/workflows/daemon.yml)
[![Build NexusHook](https://github.com/AceGuru-mjh/AutoVeil/actions/workflows/nexushook.yml/badge.svg)](https://github.com/AceGuru-mjh/AutoVeil/actions/workflows/nexushook.yml)
[![PR Status](https://img.shields.io/badge/PR-welcome-brightgreen.svg)](https://github.com/AceGuru-mjh/AutoVeil/pulls)
[![License](https://img.shields.io/badge/license-Apache--2.0-blue.svg)](LICENSE)

</div>

---

## 📖 项目定位

> **NexusCore 不是 Root 框架，是 Userspace Module Runtime。**

NexusCore **不提供 root，不提供 su**。它必须安装在有 Magisk / KernelSU / APatch 的设备上，
作为这些底层 Root 框架之上的**模块管理与运行时层**。

| 你想要… | 推荐方案 |
|---|---|
| 仅获取 root 权限 | 直接用 [Magisk](https://github.com/topjohnwu/Magisk) / [KernelSU](https://github.com/tiann/KernelSU) / [APatch](https://github.com/bmax121/APatch) |
| 开发模块，希望声明式清单 + capabilities 强制校验 + 进程隔离 | **NexusCore** |
| Zygote 注入做 Java 方法 hook | **NexusCore + NexusHook**（Rust + ptrace + 声明式 hook） |

### 🏗️ 架构层次

```
┌─────────────────────────────────────────────────────────┐
│  底层 Root 提供方（任选其一）                              │
│  Magisk · KernelSU · APatch                              │
│  - 提供 root 进程能力（CAP_SYS_ADMIN 等）                 │
│  - 提供 SELinux 策略注入工具                              │
│  - 启动 nexusd 作为 Magisk 模块 daemon                    │
└─────────────────────────────────────────────────────────┘
                    ▼ fork & exec
┌─────────────────────────────────────────────────────────┐
│  NexusCore Daemon (nexusd, C++)                          │
│  - 接管模块挂载、脚本执行、SU 策略持久化                   │
│  - 所有 root 能力通过底层 root 授权                       │
│  - 不与底层 root 的 su 冲突（共存模式）                   │
└─────────────────────────────────────────────────────────┘
        ▼ UDS IPC                ▼ ptrace + dlopen
┌──────────────────────────┐  ┌──────────────────────────┐
│  NexusManager (Kotlin)   │  │  NexusHook (Rust)        │
│  Compose UI              │  │  Zygote 注入器           │
│  - Dashboard             │  │  - 声明式 hook.toml      │
│  - Modules               │  │  - Companion 进程隔离    │
│  - SuperUser             │  │  - Namespace 隐藏        │
│  - Logs                  │  │  - 零 LD_PRELOAD 痕迹    │
│  - Settings              │  │                          │
└──────────────────────────┘  └──────────────────────────┘
```

---

## ✨ 核心特性

### 🔒 安全模型（与 Magisk 根本不同）

- **声明式模块清单（DMM）**：JSON `manifest.json` 替代 `module.prop`，支持 capabilities / intents / priority
- **Capabilities 强制校验**：未声明的能力一律拒绝执行，比 Magisk "脚本可任意作为"更安全
- **独立 Mount Namespace 脚本沙盒**：`post-fs-data.sh` / `service.sh` 在独立 NS 内执行
- **四层严格分离**：Manager 客户端绝不直接执行 root 命令，所有 root 操作经 IPC → Daemon

### 🚀 可靠性（零 Bootloop 设计）

- **任何 syscall 失败必须有 fallback**：绝不导致系统无法启动
- **OverlayFS + Bind Mount 双引擎**：自动探测内核能力，EROFS/动态分区下自动降级
- **Bootloop 保护**：连续 N 次启动失败自动禁用所有模块（safe mode）
- **Companion 进程隔离**：hook 模块崩溃只死 companion，不拖死 zygote

### 🎨 现代化 UI

- **Jetpack Compose**：Material 3 + 毛玻璃设计语言
- **MVVM + StateFlow**：所有 IPC 走 `Dispatchers.IO`，UI 永不卡顿
- **6 个核心页面**：Dashboard / Modules / SuperUser / Logs / Settings / ModuleDetail

### 🔌 Magisk 兼容性

- **shim 注入**：`ui_print` / `set_perm` / `set_perm_recursive` / `abort` / `MODPATH`
- **降低迁移成本**：现有 Magisk 模块脚本只需少量修改即可在 NexusCore 运行
- **明确不兼容项**：Zygisk `.so` 注入、`update-binary` 自定义安装器（用 NexusHook 替代）

---

## 📊 项目状态（真实情况）

> ⚠️ **当前为 MVP 阶段，不建议在生产设备使用。**

| 组件 | 状态 | 详情 |
|------|------|------|
| 📐 `specs/` 设计文档 | ✅ **完成** | 3 份 spec，约 17 万字，覆盖 daemon / manager / module SDK |
| 📱 `manager/` Android 客户端 | ✅ **UI 完整** | Jetpack Compose 6 页面，IPC + MVVM，待实机测试 |
| 🔧 `daemon/` C++ 守护进程 | 🚧 **MVP 实现** | 30+ 文件，RPC handlers 真实实现，单元测试 5 个 |
| ⚙️ `nexushook/` Rust Zygote 注入器 | 🚧 **骨架实现** | hook_table / companion / denylist 含单元测试，ptrace 注入骨架 |
| 📦 `modules/` 示例模块 | ✅ **2 个** | `nexus_prop_editor` + `nexus_hosts_editor` |
| 🌐 `web/` 知识图谱 | ✅ **完成** | 单文件 HTML |
| 🧪 单元测试 | 🚧 **部分覆盖** | daemon 5 个测试文件 / nexushook 含 #[test]，manager 待补 |
| 🤖 CI/CD | ✅ **已配置** | GitHub Actions: manager APK / daemon C++ / nexushook Rust |
| 📦 实机测试 | ❌ **未进行** | 需 Android 14+ 设备 + Magisk |

### 真实可工作的功能

- ✅ Manager 端 IPC 客户端（断线重连 + 指数退避）
- ✅ Daemon 端 IPC server + SO_PEERCRED 凭证校验
- ✅ 模块清单解析 + capabilities 校验
- ✅ OverlayFS / Bind Mount 双引擎（修复了原 spec 的 lowerdir/link bug）
- ✅ 脚本沙盒（独立 mount namespace + Magisk shim）
- ✅ Daemon RPC handlers 真实调用 DaemonCore 方法
- ✅ NexusHook TOML 解析 + companion 状态管理 + denylist glob 匹配

### 已知限制（诚实告知）

- ⚠️ NexusHook 的 ptrace 注入是骨架，未实机验证
- ⚠️ APK 签名指纹校验跳过（仅靠 UID + 包名 + SELinux 域）
- ⚠️ SU 代理模式仅本地策略持久化，未与底层 root 联动
- ⚠️ Manager 端 BiometricPrompt 已实现但未在敏感操作前调用
- ⚠️ ScriptExecutor 用 waitpid 阻塞，未实现精确超时

---

## 📂 目录结构

```
AutoVeil/                                  # 仓库（保留历史名称）
├── README.md                              # 本文件
├── .github/workflows/                     # GitHub Actions CI/CD
│   ├── manager.yml                        # 编译 Manager APK
│   ├── daemon.yml                         # 编译 + 测试 Daemon
│   ├── nexushook.yml                      # 编译 + 测试 NexusHook
│   └── ci-status.yml                      # 综合状态门控
└── nexuscore/                             # 项目本体
    ├── daemon/                            # C++ Root 守护进程
    │   ├── CMakeLists.txt
    │   ├── include/nexus/                 # 13 个头文件
    │   ├── src/                           # 19 个源文件
    │   ├── proto/nexus.proto              # IPC schema（与 manager 共享）
    │   ├── scripts/                       # Magisk 模块包装
    │   ├── tests/                         # 5 个单元测试文件
    │   └── README.md
    ├── manager/                           # Kotlin/Compose 客户端
    │   └── app/src/main/...
    ├── nexushook/                         # 🆕 Rust Zygote 注入器
    │   ├── Cargo.toml
    │   ├── src/                           # 7 个 Rust 模块
    │   ├── tests/
    │   ├── rustfmt.toml
    │   └── README.md
    ├── modules/                           # 示例模块
    │   ├── nexus_prop_editor/             # build.prop 演示
    │   └── nexus_hosts_editor/            # hosts 编辑器（推荐）
    ├── sdk/                               # 模块开发者 SDK
    │   ├── nexus_module.schema.json
    │   └── docs/developer-guide.md
    ├── specs/                             # 开发规约文档
    │   ├── spec-01-daemon.md
    │   ├── spec-02-manager.md
    │   └── spec-03-module-sdk.md
    └── web/
        └── index.html
```

---

## 🚀 快速开始

### 前置条件

1. **Android 14+ 设备**（arm64-v8a）
2. **已安装底层 Root**：[Magisk](https://github.com/topjohnwu/Magisk) / [KernelSU](https://github.com/tiann/KernelSU) / [APatch](https://github.com/bmax121/APatch)
3. **NDK r26d+**（编译 daemon）
4. **Android Studio Ladybug+**（编译 manager）
5. **Rust toolchain**（编译 nexushook，可选）

### 1️⃣ 编译 Manager APK

```bash
cd nexuscore/manager
./gradlew :app:assembleDebug
adb install app/build/outputs/apk/debug/app-debug.apk
```

### 2️⃣ 编译并部署 nexusd

```bash
export NDK=/path/to/android-ndk
cd nexuscore/daemon
cmake -B build-arm64 \
    -DCMAKE_TOOLCHAIN_FILE=$NDK/build/cmake/android.toolchain.cmake \
    -DANDROID_ABI=arm64-v8a \
    -DANDROID_PLATFORM=android-34 \
    -DANDROID_STL=c++_static
cmake --build build-arm64 -j

adb push build-arm64/nexusd /data/local/tmp/nexusd
adb shell "su -c 'mkdir -p /data/adb/nexuscore/bin && \
    cp /data/local/tmp/nexusd /data/adb/nexuscore/bin/nexusd && \
    chmod 755 /data/adb/nexuscore/bin/nexusd'"
adb reboot
```

### 3️⃣ 编译 NexusHook（可选，Zygote 注入）

```bash
rustup target add aarch64-linux-android
cd nexuscore/nexushook
# 配置 ~/.cargo/config.toml 指向 NDK clang
cargo build --release --target aarch64-linux-android
adb push target/aarch64-linux-android/release/nexushook /data/local/tmp/
adb shell "su -c 'cp /data/local/tmp/nexushook /data/adb/nexuscore/bin/'"
```

### 4️⃣ 安装示例模块

```bash
cd nexuscore/modules/nexus_hosts_editor
./build.sh
# 在 NexusManager 中"从本地 ZIP 安装" dist/nexus_nexus_hosts_editor_1.0.0.zip
```

---

## 🧪 运行测试

### Daemon 单元测试（host 平台）

```bash
cd nexuscore/daemon
cmake -B build-host -DCMAKE_BUILD_TYPE=Debug
cmake --build build-host -j --target nexusd_tests
cd build-host && ctest --verbose
```

测试覆盖：codec 编解码 · util 工具 · module_loader manifest 解析 · event_bus pub/sub · script_executor shim

### NexusHook 单元测试

```bash
cd nexuscore/nexushook
cargo test --all-features
cargo clippy --all-targets -- -D warnings
```

测试覆盖：hook_table TOML 解析 · companion 状态管理 · denylist glob · IPC varint · util

---

## 📐 设计原则

1. **仅用户态 syscall**，绝不编写内核模块 (.ko)
2. **任何 syscall 失败必须有 fallback**，绝不导致 Bootloop
3. **客户端绝不直接执行 Root 命令**，所有 Root 操作经 IPC → Daemon
4. **声明式模块清单 (DMM)**，未声明的能力一律拒绝
5. **进程隔离**，hook 模块崩溃不拖死 zygote
6. **不抄 Magisk**，借鉴架构思路但代码原创（NexusHook 用 Rust + ptrace 完全独立设计）

---

## 🆚 与主流 Root 方案对比

| 维度 | Magisk | KernelSU | APatch | **NexusCore** |
|---|:---:|:---:|:---:|:---:|
| 提供 root | ✅ | ✅ | ✅ | ❌（依赖前三者） |
| 提供 su | ✅ | ✅ | ✅ | ❌（代理模式） |
| 模块清单 | module.prop (KV) | 同 Magisk | 同 Magisk | ✅ DMM JSON |
| Capabilities 强制校验 | ❌ | ❌ | ❌ | ✅ |
| Zygote 注入 | Zygisk (C++) | ZygiskNext | Zygisk | ✅ NexusHook (Rust) |
| 进程隔离 | ❌ | ❌ | ❌ | ✅ Companion |
| DenyList 隐藏 | mount NS | mount NS | SuSFS | ✅ PID+net NS |
| 目标 Android | 8+ | 12+ GKI | ARM64 | 14+ |
| 实现语言 | C++ | C/Kernel | C/KPM | **C++ + Rust + Kotlin** |

---

## 📚 开发规约

- [Spec 01 — Daemon 核心架构](nexuscore/specs/spec-01-daemon.md)
- [Spec 02 — NexusManager](nexuscore/specs/spec-02-manager.md)
- [Spec 03 — Module SDK](nexuscore/specs/spec-03-module-sdk.md)
- [NexusHook 设计文档](nexuscore/nexushook/README.md)
- [开发者指南](nexuscore/sdk/docs/developer-guide.md)

---

## 🤝 贡献

详见 [CONTRIBUTING.md](CONTRIBUTING.md)（待补）。当前最需要的贡献方向：

- 🔴 NexusHook ptrace 注入实机验证
- 🔴 Daemon RPC handlers 真实逻辑对接 DaemonCore（已部分完成）
- 🟡 Manager 端单元测试
- 🟡 CI/CD pipeline 优化
- 🟡 Magisk 模块仓库索引（Phase 2）
- 🟢 文档翻译（英文 / 日文）

---

## 📝 命名说明

仓库 `AutoVeil` 是项目代号（最初定位自动化与隐私增强），内部代号 `NexusCore`。
GitHub 仓库保留 `AutoVeil` 不变（避免历史链接失效），所有文档与代码内部统一使用
`NexusCore` 作为产品名。

---

## 📄 License

待定（建议 Apache 2.0）。NexusHook 模块代码原创，未参考 Magisk Zygisk / Riru / LSPosed 源代码。

---

<div align="center">

**⚠️ 警告：root 设备有安全风险。本项目处于 MVP 阶段，不建议在生产设备使用。
任何因使用本项目导致的设备损坏、数据丢失，项目作者不承担责任。**

Made with ❤️ by NexusCore Team

</div>
