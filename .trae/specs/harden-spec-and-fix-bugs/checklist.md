# Checklist

## 阶段一：Daemon Spec 关键 Bug 修复

- [ ] spec-01 §6.2 OverlayFS 实现示例中所有 lowerdir/upperdir/workdir 均为目录路径，无文件路径
- [ ] spec-01 §6.2 补充说明 OverlayFS 只能目录级别挂载，替换单文件需构造目录树
- [ ] spec-01 §6.3 Bind Mount 实现示例中无 `link(2)` 调用，改为 `copyFile` (read+write)
- [ ] spec-01 §6.3 注释说明跨 fs `link()` 返回 EXDEV 的原因
- [ ] spec-01 §4.x 新增 `copyFile` 工具函数接口定义
- [ ] spec-01 §10.2 `CredentialCheck::authorize` SELinux 域匹配使用前缀匹配（`u:r:untrusted_app`）而非精确子串
- [ ] spec-01 §10.2 注释列出 Android 10/12/14/15 的实际 context 样本（如 `u:r:untrusted_app_30:s0:c512,c768`）
- [ ] spec-01 §10.4 Socket 权限方案已决策（直接 UDS 或 root-side 桥接），实现示例与方案一致
- [ ] spec-01 §10.4 说明 untrusted_app 默认不在 system 组（gid 1000），不能 connect 0660 root:system socket
- [ ] spec-01 §5.4 main.cpp 中 `detect()` 失败分支不再继续 deref `*env`，改为 return 或默认值
- [ ] spec-01 §4.1 `sepolicyWritable` 字段说明改为「可写」而非「存在」
- [ ] spec-01 §5.1 检测逻辑改为 `::access(path, W_OK) == 0`
- [ ] spec-01 新增 §10.5「Reboot RPC 实现说明」，说明 USERSPACE 通过 `sys.powerctl=userspace` 由底层 root 放行
- [ ] spec-01 §12.3 验证清单第 6 项（IPC 可连）与新的 socket 权限方案一致

## 阶段二：Manager Spec 与实现同步

- [ ] spec-02 §1.1 页面清单从 3 改为 6（Dashboard/Modules/ModuleDetail/SuperUser/Logs/Settings）
- [ ] spec-02 §2 目录结构补 viewmodel/SettingsViewModel.kt、SuRequestViewModel.kt、ModuleDetailViewModel.kt、NexusViewModelFactory.kt、ui/nav/Routes.kt、ui/components/* 等
- [ ] spec-02 §8 依赖清单补 Navigation Compose / DataStore / WorkManager / Biometric
- [ ] spec-02 §8 libs.versions.toml 示例补对应版本号
- [ ] spec-02 新增 §12「完整 RPC 清单」，列出全部 16 条 RPC
- [ ] spec-02 新增 §13「SuperUser 管理」（若决策为「做」）或 SuperUser 相关代码已删除（若决策为「不做」）
- [ ] spec-02 §1.2 非目标条款已与决策结果一致
- [ ] nexus.proto `ModuleInfo.has_update`/`update_url` 字段处理与 spec-02 §1.2 一致
- [ ] spec-01 §10.3 proto 定义补齐 Su/Reboot/UninstallFramework/ClearLogs 消息
- [ ] spec-01 与 spec-02 proto 定义完全一致

## 阶段三：示例模块修复

- [ ] modules/nexus_prop_editor/customize.sh 顶部已 shim `abort() { echo "!" "$1" >&2; exit 1; }`
- [ ] spec-03 §6.4 customize.sh 示例已同步补 abort shim
- [ ] 示例模块目标决策已落地（保留 build.prop + 限制说明 / 改为 /system/etc/hosts）
- [ ] 若保留 build.prop：README 或 manifest.description 明确 Android 14+ 上 /system/build.prop 可能只剩占位
- [ ] customize.sh 中 `set_perm` / `set_perm_recursive` shim 与 Magisk 官方语义一致
- [ ] spec-03 §6.4 注释列出所有 NexusCore shim 函数签名（ui_print/abort/set_perm/set_perm_recursive/SKIPUNZIP/MODPATH/TMPDIR/ZIPFILE）
- [ ] spec-03 §5.x 文档化 SKIPUNZIP 真实语义

## 阶段四：定位与命名统一

- [ ] nexuscore/README.md 第一段已改为「基于 Magisk/KernelSU/APatch 的用户态模块运行时」
- [ ] web/index.html hero badge 与 tagline 已更新定位
- [ ] spec-01 / spec-02 / spec-03 顶部项目定位描述一致
- [ ] 全仓库 grep `用户态 Root 框架` 无残留
- [ ] 全仓库 grep `Android Root Framework` 无残留（或仅在外部对比语境）
- [ ] 全仓库 grep `AutoVeil` 仅出现在合理位置（或全部替换为 NexusCore）
- [ ] modules/nexus_prop_editor/manifest.json `homepage` 字段已更新
- [ ] web/index.html Phase 1 状态从「● 已完成 · 90%+」改为「○ Daemon 编码未开始」
- [ ] web/index.html Phase 1 `.phase.done` class 已移除

## 阶段五：文档修复

- [ ] sdk/docs/developer-guide.md §10 已删除「service.sh 在独立 Mount NS，写入会随进程退出丢失」表述
- [ ] sdk/docs/developer-guide.md §10 改为说明 Mount NS 只隔离 mount/unmount，/data 写入正常持久化
- [ ] sdk/docs/developer-guide.md §10 补充 $NEXUS_TMPDIR 用于临时不持久数据
- [ ] spec-03 模块格式决策已落地（Magisk 兼容层 / 严格 DMM 强化强制校验）
- [ ] 若选「严格 DMM」：spec-03 §3.2 `WRITE_DATA_DIR`/`NETWORK_ACCESS` 从「仅记录」改为「强制拦截」
- [ ] 若选「Magisk 兼容」：spec-03 新增 §13「Magisk 兼容层」定义 module.prop → DMM 自动转换

## 阶段六：交叉验证

- [ ] 全仓库 grep `link(` 在 spec-01 中无跨 fs 用法
- [ ] 全仓库 grep `u:r:untrusted_app:s0` 在 spec-01 中已改为前缀匹配
- [ ] 全仓库 grep `0660 root:system` 在 spec-01 中 socket 权限已修订
- [ ] 全仓库 grep `probeFile(env.sepolicyPath` 在 spec-01 中已改为 access(W_OK)
- [ ] spec-01 与 [nexus.proto](file:///workspace/nexuscore/manager/app/src/main/proto/nexus.proto) 消息定义完全一致
- [ ] spec-02 与实际 [NexusRoot.kt](file:///workspace/nexuscore/manager/app/src/main/java/com/nexus/manager/ui/NexusRoot.kt) 路由清单一致
- [ ] spec-02 与实际 [NexusRepository.kt](file:///workspace/nexuscore/manager/app/src/main/java/com/nexus/manager/data/repo/NexusRepository.kt) 方法清单一致
- [ ] spec-03 §6.4 示例与 [modules/nexus_prop_editor/customize.sh](file:///workspace/nexuscore/modules/nexus_prop_editor/customize.sh) 实际内容一致
