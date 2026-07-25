<div align="center">

# 🛡️ AutoVeil / NexusCore

### Android 用户态核心框架 · 模块运行时 · 能力驱动

**实验性支持 AOSP arm64 Android 14+ · C++ + Rust + Kotlin**

[![Build Manager APK](https://github.com/AceGuru-mjh/AutoVeil/actions/workflows/manager.yml/badge.svg)](https://github.com/AceGuru-mjh/AutoVeil/actions/workflows/manager.yml)
[![Build & Test Daemon](https://github.com/AceGuru-mjh/AutoVeil/actions/workflows/daemon.yml/badge.svg)](https://github.com/AceGuru-mjh/AutoVeil/actions/workflows/daemon.yml)
[![Build NexusHook](https://github.com/AceGuru-mjh/AutoVeil/actions/workflows/nexushook.yml/badge.svg)](https://github.com/AceGuru-mjh/AutoVeil/actions/workflows/nexushook.yml)
[![Release](https://img.shields.io/github/v/release/AceGuru-mjh/AutoVeil)](https://github.com/AceGuru-mjh/AutoVeil/releases)

</div>

---

## 📖 项目定位

> **AutoVeil 是 Android 用户态核心框架（Userspace Core Framework）。**
>
> 不是 Magisk 替代品，不是 KernelSU 竞争者。
> 当前阶段聚焦"用户态模块运行时"做到极致，为未来 Root 后端留接口。

### 🏗️ 架构层次

```
┌─────────────────────────────────────────────┐
│  NexusManager (Kotlin/Compose)              │
│  Dashboard / Modules / SuperUser / Logs     │
└──────────────────┬──────────────────────────┘
                   │ UDS IPC
┌──────────────────┴──────────────────────────┐
│  NexusCore Daemon (C++)                     │
│  ┌─────────────┐  ┌──────────────────────┐  │
│  │ Capability  │  │ Permission Provider  │  │
│  │ Manager     │  │ (NoRoot / NexusRoot) │  │
│  └─────────────┘  └──────────────────────┘  │
│  ┌─────────────┐  ┌──────────────────────┐  │
│  │ Module      │  │ FileSystem           │  │
│  │ Loader      │  │ Interceptor          │  │
│  └─────────────┘  └──────────────────────┘  │
│  ┌─────────────┐  ┌──────────────────────┐  │
│  │ Script      │  │ Magisk Compat        │  │
│  │ Executor    │  │ Layer                │  │
│  └─────────────┘  └──────────────────────┘  │
└──────────────────┬──────────────────────────┘
                   │ ptrace + dlopen (可选)
┌──────────────────┴──────────────────────────┐
│  NexusHook (Rust) — Zygote 注入器           │
│  声明式 hook.toml + Companion 进程隔离       │
└─────────────────────────────────────────────┘
```

### 🎯 路线图

| 阶段 | 目标 | 状态 |
|---|---|---|
| **v1.0** | 用户态核心框架：模块运行时 + Capability + Permission 抽象 | ✅ 已发布 |
| **v2.0** | Root Provider 接口：支持 Magisk/KSU/APatch 作为后端 | 🚧 规划中 |
| **v3.0** | 独立 Root 框架：自研 Boot Patcher + SU 完整生命周期 | 📋 未来 |

---

## ✨ 核心特性

### 🔧 Capability 系统（新增）

daemon 启动时检测设备能力，生成能力矩阵：
- `ROOT_ACCESS` — 当前进程是否有 root 权限
- `BOOT_PATCH` — 是否支持 boot image 修补（实验性）
- `SELINUX_CONTROL` — 是否能控制 SELinux 策略
- `MOUNT_NAMESPACE` — 是否支持 mount namespace 隔离
- `ZYGOTE_HOOK` — 是否支持 zygote 注入
- `IPC_CONTROL` — 是否有 IPC server 权限
- `OVERLAY_FS` — 内核是否支持 overlayfs
- `DYNAMIC_PARTITIONS` — 是否为动态分区设备

模块和功能根据能力矩阵决定是否启用，避免在不支持的设备上崩溃。

### 🔒 Permission 抽象层（新增）

统一权限接口 `PermissionProvider`：
- `NoRootProvider` — 当前默认，禁止越权
- `NexusRootProvider` — 未来，通过 boot patch 获取 root
- `MagiskProvider` — 未来，代理 Magisk 的 root
- `KsuProvider` — 未来，代理 KernelSU 的 root

### 📦 模块系统

- **声明式清单 (DMM)**：JSON `manifest.json` + capabilities 强制校验
- **Magisk 兼容层**：自动转换 `module.prop` → DMM
- **脚本沙盒**：独立 mount namespace + Magisk shim 函数
- **文件系统拦截**：OverlayFS / Bind Mount 双引擎

### 🪝 NexusHook (Rust)

- **ptrace + dlopen 注入**（不抄 Magisk Zygisk 的 LD_PRELOAD）
- **声明式 hook.toml**（模块无需写 C++ 代码）
- **Companion 进程隔离**（模块崩溃不拖死 zygote）
- **ART hook 生成器**（DEX 注入 + ClassLoader 替换）

---

## 📊 项目状态

| 组件 | 状态 | 详情 |
|------|------|------|
| 📱 `manager/` Android 客户端 | ✅ 可编译 | Compose Material 3 + 7 页面 |
| 🔧 `daemon/` C++ 守护进程 | ✅ 可编译 | Capability + Permission + 模块系统 |
| ⚙️ `nexushook/` Rust 注入器 | ✅ 可编译 | 52 个单元测试全部通过 |
| 📦 `modules/` 示例模块 | ✅ 2 个 | `nexus_prop_editor` + `nexus_hosts_editor` |
| 🤖 CI/CD | ✅ 已配置 | manager APK / daemon C++ / nexushook Rust |
| 📦 Release | ✅ v1.0.0 | [下载 APK](https://github.com/AceGuru-mjh/AutoVeil/releases) |
| 🔬 实机测试 | ❌ 未进行 | 需 AOSP arm64 设备 |

---

## 📂 目录结构

```
AutoVeil/
├── nexuscore/
│   ├── daemon/
│   │   ├── include/nexus/
│   │   │   ├── capability/         # 🆕 能力管理器
│   │   │   ├── permission/         # 🆕 权限抽象层
│   │   │   ├── boot/               # Boot Patcher（实验性）
│   │   │   ├── ipc/                # IPC server
│   │   │   ├── fs/                 # 文件系统拦截器
│   │   │   └── ...
│   │   ├── src/
│   │   │   ├── capability/         # 🆕
│   │   │   ├── permission/         # 🆕
│   │   │   └── ...
│   │   └── tests/                  # 65 个单元测试
│   ├── manager/                    # Kotlin/Compose 客户端
│   ├── nexushook/                  # Rust Zygote 注入器
│   ├── modules/                    # 示例模块
│   ├── sdk/                        # 模块开发者 SDK
│   └── specs/                      # 开发规约
└── .github/workflows/              # CI/CD
```

---

## 🚀 快速开始

### 下载 APK

从 [Releases](https://github.com/AceGuru-mjh/AutoVeil/releases) 下载最新的 debug APK。

### 从源码编译

```bash
# Manager APK
cd nexuscore/manager && ./gradlew :app:assembleDebug

# Daemon (需要 NDK r26d+)
cd nexuscore/daemon && cmake -B build-arm64 ... && cmake --build build-arm64 -j

# NexusHook (需要 Rust)
cd nexuscore/nexushook && cargo build --release --target aarch64-linux-android
```

---

## 📐 设计原则

1. **用户态优先** — 先把用户态运行时做到极致，再进入 Boot/Kernel
2. **能力驱动** — 不同设备能力不同，功能根据能力矩阵启用
3. **权限抽象** — 通过 PermissionProvider 接口支持多种 Root 后端
4. **不抄 Magisk** — 所有核心组件原创实现
5. **零 Bootloop** — 任何 syscall 失败必须有 fallback
6. **声明式** — 模块清单 DMM + capabilities 强制校验

---

## 📄 License

Apache-2.0

---

<div align="center">

**⚠️ 实验性项目，不建议在生产设备使用。**

Made with ❤️ by NexusCore Team

</div>
