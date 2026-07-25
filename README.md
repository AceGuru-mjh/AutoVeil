<div align="center">

# 🛡️ NexusCore

### 独立 Android Root 框架 · 与 Magisk / KernelSU / APatch 同级

**Android 14+ · Rust + C++ + Kotlin · 自研 Boot Patcher · 自研 SU · 自研 Zygote 注入**

[![Build Manager APK](https://github.com/AceGuru-mjh/AutoVeil/actions/workflows/manager.yml/badge.svg)](https://github.com/AceGuru-mjh/AutoVeil/actions/workflows/manager.yml)
[![Build & Test Daemon](https://github.com/AceGuru-mjh/AutoVeil/actions/workflows/daemon.yml/badge.svg)](https://github.com/AceGuru-mjh/AutoVeil/actions/workflows/daemon.yml)
[![Build NexusHook](https://github.com/AceGuru-mjh/AutoVeil/actions/workflows/nexushook.yml/badge.svg)](https://github.com/AceGuru-mjh/AutoVeil/actions/workflows/nexushook.yml)
[![License](https://img.shields.io/badge/license-Apache--2.0-blue.svg)](LICENSE)

</div>

---

## 📖 项目定位

> **NexusCore 是独立的 Android Root 框架，与 Magisk / KernelSU / APatch 同级竞争。**
>
> 自研 Boot Image Patcher、自研 SU 授权系统、自研 SELinux 策略工具、自研 Zygote 注入器。
> 不依赖任何其他 root 框架，不抄 Magisk 代码，所有核心组件原创实现。

### 🏗️ 架构层次

```
┌─────────────────────────────────────────────────────────┐
│  NexusCore Boot Patcher (C++)                            │
│  - 修改 boot.img ramdisk                                 │
│  - 注入 nexusd 二进制 + nexus.rc service                 │
│  - 不修改 kernel，绕过 verified boot                     │
└─────────────────────────────────────────────────────────┘
                    ▼ 刷入 patched boot.img 后开机
┌─────────────────────────────────────────────────────────┐
│  init (Android)                                          │
│  - 解析 nexus.rc → 启动 nexusd service                   │
│  - on post-fs-data: nexusd --daemon                      │
│  - on boot_completed: nexusd --su-daemon                 │
└─────────────────────────────────────────────────────────┘
                    ▼ fork
┌─────────────────────────────────────────────────────────┐
│  nexusd (NexusCore Daemon, C++)                          │
│  - SELinux 策略注入（自研 nexuspolicy）                   │
│  - 模块加载（DMM manifest.json + capabilities）           │
│  - 文件系统挂载（Bind Mount / OverlayFS）                 │
│  - 脚本执行（独立 Mount Namespace + shim）                │
│  - SU 授权守护进程（/dev/socket/nexus_su.sock）           │
│  - IPC server（/dev/socket/nexusd.sock）                  │
└─────────────────────────────────────────────────────────┘
        ▼ UDS IPC                ▼ ptrace + dlopen
┌──────────────────────────┐  ┌──────────────────────────┐
│  /system/bin/su          │  │  nexushook (Rust)        │
│  - setuid root           │  │  Zygote 注入器           │
│  - 连接 nexus_su.sock    │  │  - 声明式 hook.toml      │
│  - 透传 stdin/stdout     │  │  - Companion 进程隔离    │
│                          │  │  - Namespace 隐藏        │
└──────────────────────────┘  └──────────────────────────┘
                    ▼ UDS IPC
┌─────────────────────────────────────────────────────────┐
│  NexusManager (Kotlin/Compose)                           │
│  - Dashboard / Modules / SuperUser                       │
│  - Logs / Settings / Boot Patcher                        │
│  - SuRequestActivity（独立 exported Activity）            │
└─────────────────────────────────────────────────────────┘
```

---

## ✨ 核心能力（与主流 Root 框架同级）

### 🔧 自研 Boot Image Patcher（Phase 2）

- **C++ 直接解析 Android Boot Header v0-v4**，不依赖 magiskboot / mkbootimg
- **自研 CPIO 解析器**（newc 格式），增量追加而非重组 ramdisk
- **不修改 kernel**，仅向 ramdisk 注入 nexusd + nexus.rc
- **支持 vendor_boot v3-v4**（GKI 设备）
- **SHA1 更新**（v0-v2 的 id 字段）

### 🛡️ 自研 SU 授权系统（Phase 3）

- **独立 su 二进制**（`/system/bin/su`），setuid root 但只做透传
- **真正的 root shell 由 daemon fork**，客户端无法绕过
- **策略持久化用 JSON**（Magisk 用 SQLite）
- **不依赖 AIDL**（Magisk 用 IRootServices.aidl）
- **支持 PTY 双向透传**，交互式 shell 体验

### 🚀 自研 init 注入（Phase 4）

- **nexus.rc service 定义**，init 在 post-fs-data 自动启动
- **bootstrap 脚本**首次启动标记 `.bootstrapped`
- **卸载支持**：`.uninstall_pending` 标记触发 boot 恢复

### 🔒 自研 SELinux 策略工具（Phase 5）

- **nexuspolicy** 二进制替代 magiskpolicy
- **不抄 libsepol**，自研策略注入路径
- **支持 --live 单条规则 / --apply 批量规则**

### 🪝 自研 Zygote 注入器 NexusHook（Rust）

- **ptrace + dlopen 注入**（Magisk Zygisk 用 LD_PRELOAD，完全不同）
- **声明式 hook.toml**（模块无需写 C++ 代码）
- **Companion 进程隔离**（模块崩溃不拖死 zygote）
- **PID + net namespace 隐藏**（不修改 mount table）
- **完整 ELF64 解析器**（自研，用于符号查找）

### 📦 Magisk 兼容层（Phase 5）

- **module.prop KV 解析**，自动转换为 DMM JSON
- **shim 函数注入**：`ui_print` / `set_perm` / `set_perm_recursive` / `abort` / `MODPATH`
- **现有 Magisk 模块脚本可零修改运行**（除 Zygisk 模块需用 NexusHook 替代）

### 🎨 现代化 Manager UI

- **Jetpack Compose** + Material 3 + 毛玻璃设计
- **6 个核心页面 + Boot Patcher 页面**
- **SuRequestActivity 独立 Activity**（daemon 通过 `am start` 唤起，App 后台也能弹授权）

---

## 📊 项目状态（真实情况）

| 组件 | 状态 | 详情 |
|------|------|------|
| 📐 `specs/` 设计文档 | ✅ **完成** | 3 份 spec，约 17 万字 |
| 📱 `manager/` Android 客户端 | ✅ **UI 完整** | Compose 7 页面，IPC + MVVM + 单元测试 |
| 🔧 `daemon/` C++ 守护进程 | 🚧 **MVP 实现** | 30+ 文件，Boot Patcher + SU + SELinux + 模块系统 |
| ⚙️ `nexushook/` Rust Zygote 注入器 | 🚧 **骨架实现** | ptrace + companion + denylist，含 33 个单元测试 |
| 📦 `modules/` 示例模块 | ✅ **2 个** | `nexus_prop_editor` + `nexus_hosts_editor` |
| 🤖 CI/CD | ✅ **已配置** | GitHub Actions: manager APK / daemon C++ / nexushook Rust |
| 🧪 单元测试 | 🚧 **部分覆盖** | daemon 38 个 / nexushook 33 个 / manager 60+ 个 |
| 📦 实机测试 | ❌ **未进行** | 需 Android 14+ 设备 + 解锁 bootloader |

---

## 🆚 与主流 Root 方案对比

| 维度 | Magisk | KernelSU | APatch | **NexusCore** |
|---|:---:|:---:|:---:|:---:|
| 提供 root | ✅ | ✅ | ✅ | ✅ **自研 boot patcher** |
| 提供 su | ✅ | ✅ | ✅ | ✅ **自研 su daemon** |
| SELinux 策略 | magiskpolicy | 内核 hook | KPM | ✅ **自研 nexuspolicy** |
| Zygote 注入 | Zygisk (C++) | ZygiskNext | Zygisk | ✅ **NexusHook (Rust)** |
| 模块清单 | module.prop | 同 Magisk | 同 Magisk | ✅ DMM JSON（更先进） |
| Capabilities 强制校验 | ❌ | ❌ | ❌ | ✅ |
| 进程隔离 | ❌ | ❌ | ❌ | ✅ Companion |
| Magisk 模块兼容 | ✅ | ✅ | ✅ | ✅ 兼容层 |
| Boot 修补方式 | 修改 ramdisk | 内核模块 | 内核补丁 | ✅ 修改 ramdisk（不抄 Magisk） |
| 目标 Android | 8+ | 12+ GKI | ARM64 | 14+ |
| 实现语言 | C++ | C/Kernel | C/KPM | **C++ + Rust + Kotlin** |

---

## 📂 目录结构

```
AutoVeil/                                  # 仓库
├── README.md
├── .github/workflows/                     # CI/CD
└── nexuscore/
    ├── daemon/                            # C++ Root 守护进程
    │   ├── include/nexus/
    │   │   ├── boot/boot_patcher.h        # 🆕 自研 Boot Patcher
    │   │   ├── su_daemon.h                # 🆕 自研 SU 守护进程
    │   │   ├── magisk_compat.h            # 🆕 Magisk 兼容层
    │   │   ├── ipc/                       # IPC server
    │   │   ├── fs/                        # 文件系统拦截器
    │   │   └── ...
    │   ├── src/
    │   │   ├── boot/                      # 🆕 boot_patcher + nexus_rc
    │   │   ├── su_daemon.cpp              # 🆕 SU 守护进程
    │   │   ├── su_client_main.cpp         # 🆕 su 客户端二进制
    │   │   ├── magisk_compat.cpp          # 🆕 Magisk 兼容层
    │   │   ├── nexuspolicy_main.cpp       # 🆕 SELinux 策略工具
    │   │   └── ...
    │   ├── tests/                         # 38 个单元测试
    │   └── CMakeLists.txt                 # 编译 nexusd / nexuscli / su / nexuspolicy
    ├── manager/                           # Kotlin/Compose 客户端
    │   └── app/src/main/.../pages/
    │       └── BootPatcherPage.kt         # 🆕 Boot Patcher UI
    ├── nexushook/                         # 🦀 Rust Zygote 注入器
    │   └── src/
    │       ├── ptrace_injector.rs         # ptrace + dlopen + ELF 解析
    │       ├── zygote_watcher.rs          # zygote fork 监听
    │       ├── companion_process.rs       # companion 实际 fork
    │       └── ...
    ├── modules/                           # 示例模块
    ├── sdk/                               # 模块开发者 SDK
    ├── specs/                             # 开发规约
    └── web/
```

---

## 🚀 快速开始

### 1️⃣ 编译所有组件

```bash
# Manager APK
cd nexuscore/manager && ./gradlew :app:assembleDebug

# Daemon + su + nexuspolicy（需要 NDK r26d+）
cd nexuscore/daemon
cmake -B build-arm64 \
    -DCMAKE_TOOLCHAIN_FILE=$NDK/build/cmake/android.toolchain.cmake \
    -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-34
cmake --build build-arm64 -j

# NexusHook（需要 Rust）
cd nexuscore/nexushook
cargo build --release --target aarch64-linux-android
```

### 2️⃣ 安装 NexusManager

```bash
adb install nexuscore/manager/app/build/outputs/apk/debug/app-debug.apk
```

### 3️⃣ 修补 boot.img

1. 获取设备的 boot.img（fastboot boot 备份 / 固件包提取）
2. 打开 NexusManager → 设置 → "修补 boot.img"
3. 选择 boot.img 文件
4. Manager 调用 daemon 修补，输出到 `/sdcard/Download/nexus_patched_boot.img`

### 4️⃣ 刷入修补后的 boot.img

```bash
adb reboot bootloader
fastboot flash boot /sdcard/Download/nexus_patched_boot.img
fastboot reboot
```

### 5️⃣ 验证 root

```bash
adb shell su -c id
# 应输出: uid=0(root) gid=0(root) groups=...
```

---

## 📐 设计原则

1. **独立 Root 框架**，不依赖 Magisk/KSU/APatch
2. **不抄 Magisk 代码**，所有核心组件原创实现
3. **仅用户态 syscall**，绝不编写内核模块 (.ko)
4. **任何 syscall 失败必须有 fallback**，绝不导致 Bootloop
5. **客户端绝不直接执行 Root 命令**，所有 Root 操作经 IPC → Daemon
6. **声明式模块清单 (DMM)**，未声明的能力一律拒绝
7. **进程隔离**，hook 模块崩溃不拖死 zygote

---

## 📚 开发规约

- [Spec 01 — Daemon 核心架构](nexuscore/specs/spec-01-daemon.md)
- [Spec 02 — NexusManager](nexuscore/specs/spec-02-manager.md)
- [Spec 03 — Module SDK](nexuscore/specs/spec-03-module-sdk.md)
- [NexusHook 设计文档](nexuscore/nexushook/README.md)

---

## 📄 License

Apache-2.0。所有代码原创，未参考 Magisk Zygisk / Riru / LSPosed / KernelSU / APatch 源代码。

---

<div align="center">

**⚠️ 警告：root 设备有安全风险。本项目处于 MVP 阶段，不建议在生产设备使用。**

Made with ❤️ by NexusCore Team

</div>
