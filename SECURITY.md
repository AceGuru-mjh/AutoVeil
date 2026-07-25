# 安全策略 · Security Policy

AutoVeil / NexusCore 涉及系统级 Root 操作与 SELinux 修补，安全性是本项目的首要关切。本文档说明如何安全地报告漏洞。

## 报告漏洞（推荐私有披露）

**请勿在公开 Issue / PR / Discussions 中讨论可被利用的安全漏洞**（包括但不限于：IPC 越权、SELinux 旁路、提权路径、Bootloop 触发条件中的可利用缺陷）。

请通过以下任一渠道私下报告：

1. **GitHub 私有安全公告（首选）**：前往仓库 → Security → Advisories → New draft security advisory。
2. 邮件：联系维护者（仓库 owner），主题以 `[SECURITY] NexusCore` 开头。

报告时请尽量包含：

- 受影响组件（Daemon / Manager / Module SDK / IPC 协议）
- 复现步骤、最小 PoC（如可行）
- 影响评估（可提权？可绕过 Root 校验？可导致 Bootloop？）
- 建议的修复方向

## 响应时间

| 阶段 | 目标时限 |
|---|---|
| 确认收到报告 | 3 个工作日内 |
| 初步评估 / 分类 | 7 个工作日内 |
| 修复版本发布 | 视严重程度，高危 30 天内、中危 90 天内 |
| 公开披露 | 修复发布后协调披露，或报告者同意后公开 |

## 支持的版本

本项目处于 MVP 阶段，仅对最新发布版本提供安全更新。

| 版本 | 安全更新 |
|---|---|
| 最新 Release / main | ✅ 支持 |
| 历史 Release | ❌ 请升级到最新版 |

## 安全设计基线

下列原则是项目的"严肃约束"，任何变更不得违反（详见 [CONTRIBUTING.md](./CONTRIBUTING.md)）：

1. **仅用户态 syscall**，绝不编写内核模块 (`.ko`)。
2. **任何 syscall 失败必须有 fallback**，绝不因单点失败导致 Bootloop。
3. **客户端绝不直接执行 Root 命令**，所有 Root 操作经 IPC → Daemon。
4. **声明式模块清单 (DMM)**，未声明的能力一律拒绝。
5. **最小权限 SELinux 修补**：仅放宽 `u:r:nexus_daemon:s0` 自身域，绝不全局 `setenforce 0`（仅作为最后兜底并告警）。

## 安全相关特性

- IPC 三重凭证校验：UID 白名单 + 包名 + SELinux 域 + APK 签名指纹
- `BootCounter`：连续 2 次未到 `boot_completed` 自动启用安全模式
- 模块脚本运行于独立 Mount Namespace，不污染全局挂载
- `last_mounts.json` 挂载快照支持回滚

## 致谢

感谢 responsibly 报告安全问题的研究者。经报告者同意后，已修复的漏洞将在 GitHub Security Advisories 中署名致谢。
