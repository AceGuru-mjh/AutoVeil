# NexusCore

> **Android 用户态核心框架（Userspace Core Framework）**
>
> 当前阶段聚焦"用户态模块运行时"做到极致，为未来 Root 后端留接口。

## 定位

NexusCore **不是** Magisk 替代品，**不是** KernelSU 竞争者。

当前阶段：
- 提供模块管理与运行时（DMM + capabilities + 脚本沙盒）
- 通过 Capability 系统检测设备能力
- 通过 Permission 抽象层为未来 Root 后端留接口

未来阶段：
- v2.0: Root Provider 接口（支持 Magisk/KSU/APatch 作为后端）
- v3.0: 独立 Root 框架（自研 Boot Patcher 完整生命周期）

## 目录结构

```
nexuscore/
├── daemon/      # C++ 守护进程 (nexusd)
│   ├── include/nexus/
│   │   ├── capability/     # 能力管理器
│   │   ├── permission/     # 权限抽象层
│   │   ├── boot/           # Boot Patcher（实验性）
│   │   ├── ipc/            # IPC server
│   │   ├── fs/             # 文件系统拦截器
│   │   └── ...
│   ├── src/
│   │   ├── capability/     # 能力检测
│   │   ├── permission/     # 权限提供者
│   │   └── ...
│   └── tests/              # 单元测试
├── manager/     # Kotlin/Compose 客户端
├── nexushook/   # Rust Zygote 注入器
├── modules/     # 示例模块
├── sdk/         # 模块开发者 SDK
├── specs/       # 开发规约文档
└── web/         # 知识图谱站点
```

## 设计原则

1. **用户态优先** — 先把用户态运行时做到极致，再进入 Boot/Kernel
2. **能力驱动** — 不同设备能力不同，功能根据能力矩阵启用
3. **权限抽象** — 通过 PermissionProvider 接口支持多种 Root 后端
4. **不抄 Magisk** — 所有核心组件原创实现
5. **零 Bootloop** — 任何 syscall 失败必须有 fallback
6. **声明式** — 模块清单 DMM + capabilities 强制校验

## 兼容性

- **实验性支持** AOSP arm64 Android 14+
- 不保证小米/三星/OPPO 等厂商设备兼容
- 不建议在生产设备使用

## 目标系统

Android 14+ (API 34+), arm64-v8a（实验性）
