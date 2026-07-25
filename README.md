<div align="center">
  <img src="./assets/logo.svg" width="128" alt="AutoVeil / NexusCore logo"/>
  <h1>AutoVeil · NexusCore</h1>
  <p>面向 Android 14–16 的用户态 Root 框架 · 四层严格分离架构 · 零 Bootloop 设计</p>

  <img src="https://img.shields.io/github/stars/AceGuru-mjh/AutoVeil?style=flat-square&logo=github&color=00e5ff" alt="stars"/>
  <img src="https://img.shields.io/github/forks/AceGuru-mjh/AutoVeil?style=flat-square&logo=github" alt="forks"/>
  <img src="https://img.shields.io/github/watchers/AceGuru-mjh/AutoVeil?style=flat-square&logo=github" alt="watchers"/>
  <img src="https://img.shields.io/github/license/AceGuru-mjh/AutoVeil?style=flat-square&color=blue" alt="license"/>
  <img src="https://img.shields.io/github/v/release/AceGuru-mjh/AutoVeil?style=flat-square&include_prereleases" alt="release"/>
  <img src="https://img.shields.io/github/last-commit/AceGuru-mjh/AutoVeil?style=flat-square&logo=git" alt="last-commit"/>
  <br/>
  <img src="https://img.shields.io/github/actions/workflow/status/AceGuru-mjh/AutoVeil/build-apk.yml?branch=main&style=flat-square&logo=githubactions&label=APK%20Build" alt="Build APK"/>
  <img src="https://img.shields.io/badge/Android-14%20%7C%2015%20%7C%2016-3ddc84?style=flat-square&logo=android&logoColor=white" alt="Android"/>
  <img src="https://img.shields.io/badge/Kotlin-2.0.21-7f52ff?style=flat-square&logo=kotlin&logoColor=white" alt="Kotlin"/>
  <img src="https://img.shields.io/badge/C%2B%2B-17-00599c?style=flat-square&logo=c%2B%2B&logoColor=white" alt="C++"/>
  <img src="https://img.shields.io/badge/Compose-Material3-00e5ff?style=flat-square" alt="Compose"/>
  <img src="https://img.shields.io/badge/ABI-arm64--v8a-ff69b4?style=flat-square" alt="ABI"/>
  <img src="https://img.shields.io/badge/Min%20API-34%2B-3ddc84?style=flat-square" alt="minSdk"/>
</div>

<p align="center">
  <a href="#-项目简介">简介</a> ·
  <a href="#-架构">架构</a> ·
  <a href="#-核心特性">特性</a> ·
  <a href="#-快速开始">快速开始</a> ·
  <a href="#-开发与构建">开发构建</a> ·
  <a href="#-自动化-ci">CI</a> ·
  <a href="#-路线图">路线图</a> ·
  <a href="#-开发规约">Spec</a> ·
  <a href="#-参与贡献">贡献</a> ·
  <a href="#-许可证">许可证</a>
</p>

<p align="center">
  <a href="./README_en.md">English</a> · 简体中文
</p>

---

## 📖 项目简介

**AutoVeil / NexusCore** 是一款面向 Android 14 / 15 / 16 的**用户态 Root 框架**，采用严格的四层分离架构：C++ Root 守护进程 `nexusd` + Kotlin/Compose 管理客户端 + 模块开发者 SDK + 知识图谱站点。

它解决了系统级 Root 改造中三个最棘手的问题：**Bootloop 风险**、**SELinux 强制策略冲突**、**模块冲突与权限滥用**。

- 🛡️ **零 Bootloop**：仅用户态 syscall，绝不编写内核模块；任何 syscall 失败均有 fallback，连续失败 3 次自动进入安全模式。
- 🔐 **最小权限 SELinux 修补**：仅放宽 `u:r:nexus_daemon:s0` 自身域，绝不全局 `setenforce 0`。
- 🧩 **声明式模块清单 (DMM)**：模块通过 `manifest.json` 声明能力与意图，**未声明的能力一律拒绝**。
- 🔌 **进程隔离**：所有模块脚本在独立 Mount Namespace 内执行，不污染全局挂载。
- 📡 **安全 IPC**：Unix Domain Socket + Protobuf + `SO_PEERCRED` 凭证校验 + APK 签名指纹双校验。
- 🎨 **原生 Compose UI**：Material3 深色界面，edge-to-edge 毛玻璃，实时日志流。

> 适用人群：Android 系统级工具开发者、Root 模块作者、隐私增强与自动化爱好者。

---

## 🏛️ 架构

<div align="center">
  <img src="./assets/architecture.svg" width="880" alt="NexusCore 四层分离架构"/>
</div>

四层职责严格隔离，**客户端绝不直接执行 Root 命令**，所有 Root 操作经 IPC → Daemon：

| 层 | 语言 | 职责 |
|---|---|---|
| **NexusManager** | Kotlin / Compose | 状态面板、模块管理、日志流、超级用户、设置；仅 IPC 客户端 |
| **NexusDaemon (nexusd)** | C++17 | init 启动的 Root 守护进程：SELinux 修补、FS 拦截、模块加载、IPC Server |
| **Module SDK** | JSON Schema / Shell | DMM 清单规范、capabilities 白名单、脚本运行环境约定 |
| **Web 知识图谱** | HTML/CSS | 单页站点，呈现架构与演进路线图 |

**启动时序**：`init → nexusd --daemon → RootEnvironmentDetector → SELinuxManager.patchSelfDomain → ModuleLoader.scanModules → FileSystemInterceptor.mountAll → IpcServer.listen → late_start 脚本 → Watchdog 主循环`

---

## ✨ 核心特性

### 🔧 Daemon 守护进程
- 双 fork `daemonize` + PID 文件（`O_CREAT|O_EXCL`）防双开
- 线程级 Watchdog，连续 3 次重启失败主动 `_exit(1)` 触发 init 重启
- `RootEnvironmentDetector` 自动识别 Magisk / KernelSU / APatch
- 文件系统拦截三段降级：`OverlayFS → Bind Mount → 只读 Noop`
- 动态分区 / EROFS 兼容：bind 是 VFS 层操作，与底层 FS 无关

### 🛡️ 安全与防 Bootloop
- 仅修补 `nexus_daemon` 域，保留 Enforcing；最后手段才 `setenforce 0` 并告警
- `BootCounter`：连续 2 次未到 `boot_completed` 自动启用安全模式
- `last_mounts.json` 挂载快照，支持回滚
- IPC 三重校验：UID 白名单 + 包名 + SELinux 域 + APK 签名指纹

### 📱 NexusManager 客户端
- MVVM + StateFlow，所有 IPC 走 `Dispatchers.IO`，UI 零阻塞
- 断线指数退避重连（500ms → 15s 上限 + 抖动）
- 实时日志流：`SharedFlow` 多订阅，2000 行滑动窗口防 OOM
- 本地 ZIP 模块安装：`FileBridge` 复制到 `/data/local/tmp` 供 Daemon 读取
- 5 个核心页面：Dashboard / Modules / Logs / SuperUser / Settings

### 🧩 模块 SDK
- `manifest.json` (DMM) 声明 `capabilities` 与 `intents`
- 能力白名单：`EXECUTE_SHELL` / `MODIFY_SYSTEM_PROPS` / `MOUNT_FILESYSTEM` 等
- 脚本阶段：`post-fs-data.sh` / `service.sh` / `customize.sh` / `uninstall.sh` / `verify.sh`
- 环境变量注入：`NEXUS_MODULE_PATH`、`NEXUS_BOOT_STAGE`、`NEXUS_ROOT_PROVIDER` 等
- 示例模块 `nexus_prop_editor`：安全修改 `build.prop` 的 `ro.debuggable` / `ro.secure`

---

## 🚀 快速开始

### 方式一：使用 Release 成品

1. 前往 [Releases](https://github.com/AceGuru-mjh/AutoVeil/releases) 下载最新 NexusManager APK 与 nexusd 二进制。
2. 通过 Magisk / KernelSU / APatch 的 service.d 机制部署 `nexusd`（详见 [Spec 01 §12](./nexuscore/specs/spec-01-daemon.md)）。
3. 安装 NexusManager APK，打开后自动连接 Daemon。

> MVP 阶段：Daemon 通过 Magisk module 包装落地二进制 + `service.d/nexusd.rc`，**无需修改 boot.img**。

### 方式二：本地源码构建

**环境要求**：Android Studio Hedgehog+ · JDK 17 · Android SDK 35 · NDK r26d+ · CMake ≥ 3.22

```bash
# 1. 克隆仓库
git clone https://github.com/AceGuru-mjh/AutoVeil.git
cd AutoVeil

# 2. 构建 NexusManager APK
cd nexuscore/manager
chmod +x ./gradlew
./gradlew :app:assembleDebug      # debug APK
# 或 ./gradlew :app:assembleRelease

# 产物路径：
# nexuscore/manager/app/build/outputs/apk/debug/app-debug.apk

# 3. 构建 nexusd 守护进程（需 NDK）
cd ../daemon
./build.sh arm64-v8a              # 产物：build-arm64-v8a/nexusd
```

### 方式三：构建示例模块 ZIP

```bash
cd nexuscore/modules
./nexus_prop_editor/build.sh
# 产物：dist/nexus_nexus_prop_editor_1.0.0.zip
```

---

## 🛠️ 开发与构建

### 项目目录结构

```
AutoVeil/
├── .github/
│   ├── workflows/build-apk.yml     # CI：自动构建 APK
│   ├── ISSUE_TEMPLATE/             # Bug / Feature 模板
│   └── PULL_REQUEST_TEMPLATE.md    # PR 模板（含严肃约束自检）
├── assets/                         # Logo、架构图
├── nexuscore/
│   ├── daemon/                     # C++17 Root 守护进程 (nexusd)
│   │   ├── src/                    # core / selinux / fs / module / ipc
│   │   └── proto/nexus.proto       # IPC schema
│   ├── manager/                    # Kotlin/Compose 客户端
│   │   ├── app/src/main/java/com/nexus/manager/
│   │   │   ├── ipc/                # UDS 传输 + Protobuf 编解码 + 重连
│   │   │   ├── data/               # Repository + UI Model
│   │   │   ├── viewmodel/          # MVVM + StateFlow
│   │   │   └── ui/                 # Compose pages + theme
│   │   └── gradle/libs.versions.toml
│   ├── modules/
│   │   └── nexus_prop_editor/      # 示例模块（manifest + scripts）
│   ├── sdk/
│   │   ├── docs/developer-guide.md # 模块开发者指南
│   │   └── nexus_module.schema.json# DMM JSON Schema
│   ├── specs/                      # 开发规约（Spec 01/02/03）
│   └── web/index.html              # 知识图谱单页站点
├── README.md                       # 本文档
├── README_en.md                    # 英文文档
├── CONTRIBUTING.md                 # 贡献指南
└── LICENSE                         # GPL-3.0
```

### 部署到设备并验证

```bash
adb root
adb push build-arm64-v8a/nexusd /data/local/tmp/nexusd
adb shell "chmod 755 /data/local/tmp/nexusd"
# 前台调试（输出到 logcat）
adb shell "/data/local/tmp/nexusd --foreground"
```

完整验证清单（10 项，含 SELinux 仍 Enforcing、5 次冷启动不 Bootloop）见 [Spec 01 §12.3](./nexuscore/specs/spec-01-daemon.md)。

---

## 🤖 自动化 CI

本项目配置 [GitHub Actions](./.github/workflows/build-apk.yml)：

- 触发：push 到 `main` / `feat/**` / `trae/**`，或针对 `main` 的 PR，或手动 `workflow_dispatch`
- 任务：JDK 17 + Android SDK 35 + `./gradlew :app:assembleDebug/Release`
- 产物：debug / release APK 作为 artifact 上传，保留 30 天；构建报告保留 7 天
- 并发：同一 ref 的运行自动取消旧任务（`cancel-in-progress`）

---

## 🗺️ 路线图

| Phase | 主题 | 状态 |
|---|---|---|
| 1 | MVP：Daemon + Manager + 模块 SDK 基线 | ✅ Approved baseline |
| 2 | 模块签名强制、在线仓库索引 | 🚧 规划中 |
| 3 | 事件总线内存缝合（多模块同属性叠加） | 📐 设计中 |
| 4 | Lua / Wasm 脚本引擎、Agent 运行时 | 🔬 探索中 |
| 5 | App 级隔离沙盒（原"平行宇宙"收缩版） | 🔬 探索中 |

详见 [web/index.html](./nexuscore/web/index.html) 知识图谱站点。

---

## 📚 开发规约

架构与实现细节由三份 Spec 文档严格定义，任何 PR 不得违反其中的"严肃约束"：

- 📄 [Spec 01 — Daemon 核心架构](./nexuscore/specs/spec-01-daemon.md) — 启动时序、SELinux 修补、FS 拦截、IPC 协议、安全模式
- 📄 [Spec 02 — NexusManager](./nexuscore/specs/spec-02-manager.md) — IPC 客户端、Repository、ViewModel、Compose UI
- 📄 [Spec 03 — Module SDK](./nexuscore/specs/spec-03-module-sdk.md) — DMM 清单、capabilities、脚本运行环境、示例模块
- 📘 [模块开发者指南](./nexuscore/sdk/docs/developer-guide.md) — 第三方模块开发从 0 到 1

---

## 🤝 参与贡献

欢迎提交 Issue 与 PR！提交前请阅读 [贡献指南](./CONTRIBUTING.md)。

1. Fork 本仓库
2. 从最新 `main` 切分支：`feat/<scope>-<short-desc>` 或 `fix/<scope>-<short-desc>`
3. 遵循 [Conventional Commits](https://www.conventionalcommits.org/) 提交规范
4. 按 [PR 模板](./.github/PULL_REQUEST_TEMPLATE.md) 填写，完成"严肃约束自检"
5. 等待 CI 通过并回应 Review 意见

### 🐛 Bug 反馈

提交 Issue 请附带：设备型号、Android 版本、Root 来源 (Magisk/KSU/APatch) 及版本、FS 拦截器类型、复现步骤，以及日志：

```bash
adb shell cat /data/adb/nexuscore/nexusd.log
```

> 若导致 Bootloop，可先进入安全模式：在 `/data/adb/nexuscore/` 下创建 `safe_mode` 文件后重启。

---

## 📄 许可证

本项目基于 [**GPL-3.0**](./LICENSE) 协议开源，Copyright © 2026 AutoVeil / NexusCore Contributors。

选择 GPL-3.0 是为了与 Android Root 生态（Magisk / KernelSU / LSPosed 等）保持一致：衍生作品必须以同等协议开源，确保整个生态的可审计性与自由度。

> ⚠️ 本项目涉及系统级 Root 操作，使用者需自行承担风险。作者不对因使用本工具导致的任何设备损坏、数据丢失或 Bootloop 负责。请务必先阅读各 Spec 的"风险与缓解"章节，并在测试设备上验证。
