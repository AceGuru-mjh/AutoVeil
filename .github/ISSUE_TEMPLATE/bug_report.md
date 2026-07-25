---
name: Bug 报告
about: 报告 AutoVeil / NexusCore 的缺陷或异常行为
title: "[BUG] "
labels: bug
---

## Bug 描述

<!-- 简要说明遇到了什么问题 -->


## 复现步骤

1.
2.
3.

## 预期行为

<!-- 应该发生什么 -->


## 实际行为

<!-- 实际发生了什么 -->


## 环境信息

- 设备型号：
- Android 版本：
- Root 来源 (Magisk / KernelSU / APatch)：
- Root 版本：
- NexusCore / NexusManager 版本：
- FS 拦截器 (overlayfs / bind / noop)：

## 日志

<!-- 非常重要，请附上相关日志 -->
<!-- 获取方式：adb shell cat /data/adb/nexuscore/nexusd.log -->
<!-- Manager 日志页导出也可 -->

```
粘贴日志到这里
```

## 是否导致 Bootloop

- [ ] 是（设备无法开机 / 反复重启）
- [ ] 否

> 若导致 Bootloop，请勿在公开 Issue 讨论可被利用的细节，可先进入安全模式：在 `/data/adb/nexuscore/` 下创建 `safe_mode` 文件后重启。
