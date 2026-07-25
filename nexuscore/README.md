# NexusCore

> **建立在 Magisk / KernelSU / APatch 之上的用户态模块运行时（userspace module runtime）**
>
> ⚠️ NexusCore **不提供 root，不提供 su**。它必须安装在有 Magisk / KSU / APatch 的设备上，
> 作为这些底层 root 框架的"模块管理与运行时层"。

## 目录结构

```
nexuscore/
├── daemon/      # C++ Root 守护进程 (nexusd)
├── manager/     # Kotlin/Compose 客户端
├── sdk/         # 模块开发者 SDK
├── modules/     # 示例模块
├── specs/       # 开发规约文档
└── web/         # 知识图谱站点
```

## 开发规约

- [Spec 01 — Daemon 核心架构](specs/spec-01-daemon.md)
- [Spec 02 — NexusManager](specs/spec-02-manager.md)
- [Spec 03 — Module SDK](specs/spec-03-module-sdk.md)

## 设计原则

1. 仅用户态 syscall，绝不编写内核模块 (.ko)
2. 任何 syscall 失败必须有 fallback，绝不导致 Bootloop
3. 客户端绝不直接执行 Root 命令，所有 Root 操作经 IPC → Daemon
4. 声明式模块清单 (DMM)，未声明的能力一律拒绝

## 目标系统

Android 14 / 15 / 16 (API 34+), arm64-v8a

## 与底层 Root 的关系

详见 [Spec 01 §14](specs/spec-01-daemon.md#14-补充章节与底层-root-的关系定位)。
简言之：底层 root 提供 root 进程能力 + SELinux 策略注入工具，NexusCore Daemon
作为 Magisk/KSU 模块在 boot 时被启动，接管模块挂载、脚本执行、SU 策略持久化等用户态工作。
NexusCore 不与底层 root 的 su 冲突（共存模式）。
