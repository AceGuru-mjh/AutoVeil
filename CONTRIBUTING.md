# 贡献指南 · Contributing to AutoVeil / NexusCore

感谢你对 AutoVeil / NexusCore 的关注！本文档说明如何参与本项目开发。提交 Issue 或 PR 即视为你同意遵守以下约定。

> Read this in English: contributions are welcome — please open an issue first for large changes, fork the repo, branch from `main`, and submit a PR following the templates.

---

## 一、行为准则

- 保持友善、尊重，针对问题而非个人。
- 技术讨论基于 Spec 与代码事实，避免主观偏好争论。
- 安全相关问题（可被利用的漏洞、Bootloop 风险）请勿在公开 Issue 讨论，私下联系维护者。

---

## 二、开始之前

1. 阅读 [nexuscore/README.md](./nexuscore/README.md) 了解四层架构与设计原则。
2. 阅读相关 Spec：
   - [Spec 01 — Daemon](./nexuscore/specs/spec-01-daemon.md)
   - [Spec 02 — Manager](./nexuscore/specs/spec-02-manager.md)
   - [Spec 03 — Module SDK](./nexuscore/specs/spec-03-module-sdk.md)
3. 留意四条**严肃约束**，任何 PR 违反将被拒绝：
   - 仅用户态 syscall，绝不编写内核模块 (`.ko`)
   - 任何 syscall 失败必须有 fallback，绝不导致 Bootloop
   - 客户端绝不直接执行 Root 命令，所有 Root 操作经 IPC → Daemon
   - 声明式模块清单 (DMM)，未声明的能力一律拒绝

---

## 三、开发环境

| 项 | 要求 |
|---|---|
| Android Studio | Hedgehog (2023.1.1) 或更高 |
| JDK | 17 |
| Android SDK | compileSdk 35 / targetSdk 35 / minSdk 34 |
| Kotlin | 2.0.21 |
| NDK | r26d+ (Daemon C++ 交叉编译) |
| CMake | ≥ 3.22 |
| 目标 ABI | `arm64-v8a` (主)，`x86_64` (模拟器调试) |

---

## 四、分支与提交规范

### 分支

- `main`：稳定分支，**禁止直接提交**，所有变更经 PR 合入。
- `feat/<scope>-<short-desc>`：新功能，如 `feat/manager-biometric`
- `fix/<scope>-<short-desc>`：Bug 修复，如 `fix/daemon-reconnect`
- `docs/<short-desc>`：文档
- `refactor/<scope>-<short-desc>`：重构（无行为变化）

### Commit Message

使用 [Conventional Commits](https://www.conventionalcommits.org/) 前缀：

```
<type>(<scope>): <简短描述>

<可选正文，说明动机与影响>
```

| type | 用途 |
|---|---|
| `feat` | 新功能 |
| `fix` | Bug 修复 |
| `docs` | 文档 |
| `refactor` | 重构 |
| `perf` | 性能优化 |
| `test` | 测试 |
| `ci` | CI/CD |
| `chore` | 杂项 |

示例：
```
feat(manager): add biometric unlock for superuser page
fix(daemon): fallback to bind mount when overlayfs probe fails
docs(spec-03): clarify capabilities validation timing
```

---

## 五、提交 PR 的流程

1. **先开 Issue**：新功能或较大改动，先开 Issue 讨论方案，避免做无用功。
2. Fork 仓库并克隆：
   ```bash
   git clone https://github.com/<你的用户名>/AutoVeil.git
   cd AutoVeil
   git remote add upstream https://github.com/AceGuru-mjh/AutoVeil.git
   ```
3. 从最新 `main` 切分支：
   ```bash
   git checkout main && git pull upstream main
   git checkout -b feat/your-feature
   ```
4. 编码 → 本地构建验证：
   ```bash
   # Manager APK
   cd nexuscore/manager && ./gradlew :app:assembleDebug
   # Daemon（需 NDK）
   cd ../daemon && ./build.sh arm64-v8a
   ```
5. 提交，保持 commit 原子化、描述清晰。
6. 推送并发起 PR，目标分支 `main`，**按 PR 模板填写**。
7. 等待 CI（GitHub Actions 构建 APK）通过，回应 Review 意见。

---

## 六、代码风格

### Kotlin (Manager)
- 遵循 [Kotlin Coding Conventions](https://kotlinlang.org/docs/coding-conventions.html) 与 Android 官方指南。
- 所有 IPC 调用走 `Dispatchers.IO`，ViewModel 只暴露 `StateFlow`，不得在 UI 线程阻塞。
- 公共 API 写 KDoc；复杂逻辑加注释说明 *为什么* 而非 *是什么*。

### C++ (Daemon)
- C++17，`-Wall -Wextra -Werror`。
- 所有 syscall 包装返回 `Result<T>`（`std::expected`），禁止裸 `int` 返回 + 全局 errno。
- 日志走 `NX_LOG_*` 宏，**绝不写 stderr**（init 早期未重定向会丢失）。

### Shell 脚本 (Modules)
- 必须以 `#!/system/bin/sh` 开头（POSIX sh，不支持 bashism）。
- 单脚本最长 120 秒，必须可重入。

---

## 七、测试与验证

- Daemon 改动：在 Android 14+ 真机或模拟器完成 [Spec 01 §12.3 验证清单](./nexuscore/specs/spec-01-daemon.md)。
- Manager 改动：完成 [Spec 02 §9 验收标准](./nexuscore/specs/spec-02-manager.md)，至少验证 5 次冷启动不 Bootloop。
- 模块 SDK 改动：示例模块 `nexus_prop_editor` 必须能被正确安装、生效、卸载。

---

## 八、Issue 与 PR 模板

- Bug 反馈：使用 `.github/ISSUE_TEMPLATE/bug_report.md`
- 功能需求：使用 `.github/ISSUE_TEMPLATE/feature_request.md`
- PR：使用 `.github/PULL_REQUEST_TEMPLATE.md`

提交 Issue 请务必附带：设备型号、Android 版本、Root 来源 (Magisk/KSU/APatch) 及版本、复现步骤、相关日志（`adb shell cat /data/adb/nexuscore/nexusd.log`）。

---

## 九、许可证

提交的代码将在 [GPL-3.0](./LICENSE) 下发布。提交 PR 即表示你同意以该许可证贡献代码，且你有权做出此授权。
