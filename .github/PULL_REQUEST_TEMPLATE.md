<!--
感谢你提交 PR！请先确认以下事项，再填写变更说明。
详见 CONTRIBUTING.md。所有 Root 操作必须经 IPC → Daemon，禁止客户端直接执行 Root 命令。
-->

## 变更说明

<!-- 这个 PR 做了什么？ -->


## 动机 / 背景

<!-- 为什么要做这个改动？关联 Issue：Fixes #xxx / Refs #xxx -->


## 变更类型

- [ ] 新功能 (feat)
- [ ] Bug 修复 (fix)
- [ ] 文档 (docs)
- [ ] 重构 (refactor)
- [ ] 性能 (perf)
- [ ] CI/构建 (ci)
- [ ] 其他 (chore)

## 严肃约束自检

> 违反任一项将被拒绝，详见 CONTRIBUTING.md。

- [ ] 未编写任何内核模块 (`.ko`)，仅用户态 syscall
- [ ] syscall 失败有 fallback，不会导致 Bootloop
- [ ] 客户端未直接执行 Root 命令（如有 Root 操作，走 IPC → Daemon）
- [ ] 模块能力遵循 DMM 声明式清单（未声明的能力未使用）

## 验证方式

<!-- 如何验证本次改动？构建命令、测试步骤、设备信息 -->

- [ ] 本地构建通过（Manager: `./gradlew :app:assembleDebug`；Daemon: `./build.sh arm64-v8a`）
- [ ] CI (GitHub Actions) 通过
- [ ] 真机/模拟器验证（如涉及运行时行为）

设备信息（如适用）：
- 机型：
- Android 版本：
- Root 来源 / 版本：

## 影响范围 / Breaking Changes

- [ ] 本 PR 包含不兼容变更
- [ ] 已更新对应 Spec / 文档
