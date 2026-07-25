# NexusCore

Android 14-16 用户态 Root 框架，严格四层分离架构。

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
