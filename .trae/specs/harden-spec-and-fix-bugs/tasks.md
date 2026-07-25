# Tasks

## 阶段一：Daemon Spec 关键 Bug 修复（spec-01，纯文档修改，不写代码）

- [ ] Task 1: 修复 spec-01 §6.2 OverlayFS lowerdir 用法
  - [ ] SubTask 1.1: 阅读现有 §6.2 `OverlayFsInterceptor::mountOverlay` 实现，确认 lowerdir 被错误地传为文件路径
  - [ ] SubTask 1.2: 重写 §6.2 的实现示例：替换单文件时构造「目标父目录 + 同名文件」的目录树，挂载到父目录级别
  - [ ] SubTask 1.3: 补充 §6.2 注释说明 OverlayFS 只能目录级别挂载，并指向 §6.5 的 EROFS 处理
- [ ] Task 2: 修复 spec-01 §6.3 Bind Mount 跨 fs 硬链接 bug
  - [ ] SubTask 2.1: 阅读现有 §6.3 `BindMountInterceptor::mountOverlay`，确认 `link(2)` 用法错误
  - [ ] SubTask 2.2: 将备份逻辑从 `link()` 改为 `copyFile()`（read + write），并说明跨 fs（EROFS→ext4）会返回 EXDEV 的原因
  - [ ] SubTask 2.3: 补充 `copyFile` 工具函数的接口定义到 §4 公共基础类型
- [ ] Task 3: 修复 spec-01 §10.2 SO_PEERCRED SELinux context 匹配
  - [ ] SubTask 3.1: 阅读现有 `CredentialCheck::authorize`，确认 `find("u:r:untrusted_app:s0")` 匹配规则对 Android 10+ context 失效
  - [ ] SubTask 3.2: 改为前缀匹配 `u:r:untrusted_app` / `u:r:platform_app` / `u:r:system_app`，兼容 `untrusted_app_30:s0:c512,c768` 格式
  - [ ] SubTask 3.3: 补充测试场景说明：列出 Android 10/12/14/15 的实际 context 样本
- [ ] Task 4: 修复 spec-01 §10.4 Socket 权限 catch-22
  - [ ] SubTask 4.1: 确认 `chmod 0660 + chown root:system` 让 untrusted_app 无法 connect 的问题
  - [ ] SubTask 4.2: 调研两种方案并选其一写入 spec：(A) Magisk libsu 风格 root-side 桥接；(B) socket 0660 root:shell + Manager 加 shell 组（需 platform 签名或 root 注入）
  - [ ] SubTask 4.3: 更新 §10.4 实现示例与 §12.3 验证清单第 6 项
- [ ] Task 5: 修复 spec-01 §5.4 main.cpp 解引用失败 Result 的 UB
  - [ ] SubTask 5.1: 阅读现有 main.cpp，确认 `if (!env) { /* 注释 */ } SELinuxManager se(*env);` 模式是 UB
  - [ ] SubTask 5.2: 改为：失败时显式构造默认 `RootEnvironment{provider=Unknown}` 进入只读模式，或直接 `return`
  - [ ] SubTask 5.3: 补充 §5.4 注释说明只读模式下哪些 stage 必须跳过
- [ ] Task 6: 修复 spec-01 §4.1 与 §5.1 `sepolicyWritable` 语义
  - [ ] SubTask 6.1: 确认 `probeFile` 只检测存在不检测可写
  - [ ] SubTask 6.2: 改为 `::access(path, W_OK) == 0`
  - [ ] SubTask 6.3: 更新 §5.1 检测逻辑伪代码
- [ ] Task 7: 补充 spec-01 USERSPACE 重启实现路径
  - [ ] SubTask 7.1: 新增 §10.5「Reboot RPC 实现说明」
  - [ ] SubTask 7.2: 说明 `sys.powerctl=userspace` 需底层 root 已注入 SELinux 规则放行
  - [ ] SubTask 7.3: 说明 NexusCore 自身不直接调 setprop，而是 fork shell 调用，依赖底层 root 的策略

## 阶段二：Manager Spec 与实现同步（spec-02）

- [ ] Task 8: 同步 spec-02 页面清单
  - [ ] SubTask 8.1: 阅读现有 [NexusRoot.kt](file:///workspace/nexuscore/manager/app/src/main/java/com/nexus/manager/ui/NexusRoot.kt) 与 pages/ 目录，确认实际 6 页面
  - [ ] SubTask 8.2: 更新 spec-02 §1.1 从「3 页面」改为「6 页面」
  - [ ] SubTask 8.3: 补充 §6.x 章节描述 SuperUserPage / SettingsPage / ModuleDetailPage 的设计与 ViewModel
- [ ] Task 9: 同步 spec-02 RPC 清单
  - [ ] SubTask 9.1: 阅读 [nexus.proto](file:///workspace/nexuscore/manager/app/src/main/proto/nexus.proto)，列出所有实际 RPC
  - [ ] SubTask 9.2: 新增 spec-02 §12「完整 RPC 清单」，逐条说明调用者/权限/返回值
  - [ ] SubTask 9.3: 对比 spec-01 §10.3 的 proto 定义，确保两端一致（spec-01 也需补 Su/Reboot/UninstallFramework 消息）
- [ ] Task 10: 同步 spec-02 依赖与目录结构
  - [ ] SubTask 10.1: 阅读实际 [build.gradle.kts](file:///workspace/nexuscore/manager/app/build.gradle.kts) 与 [libs.versions.toml](file:///workspace/nexuscore/manager/gradle/libs.versions.toml)
  - [ ] SubTask 10.2: 更新 spec-02 §8 依赖清单：补 Navigation / DataStore / WorkManager / Biometric
  - [ ] SubTask 10.3: 更新 spec-02 §2 目录结构，补 viewmodel/SettingsViewModel.kt、SuRequestViewModel.kt、ModuleDetailViewModel.kt、NexusViewModelFactory.kt 等
- [ ] Task 11: SU 管理产品决策与 spec 修订
  - [ ] SubTask 11.1: 与用户确认决策方向（做自己的 SU vs 删除 SU 相关代码）—— 此步需 AskUserQuestion
  - [ ] SubTask 11.2: 若选「做」：删除 spec-02 §1.2「Root UI 是非目标」条款，新增 §13「SuperUser 管理」说明与底层 root 的协作关系
  - [ ] SubTask 11.3: 若选「不做」：删除 SuperUserPage / SuRequestDialog / SuRequestViewModel 与 proto 中 Su 相关消息，回退到 spec-02 原状态
- [ ] Task 12: ModuleInfo.has_update / update_url 字段决策
  - [ ] SubTask 12.1: 二选一：(A) 删除字段与 spec 一致；(B) 保留字段并更新 spec
  - [ ] SubTask 12.2: 执行删除或更新

## 阶段三：示例模块修复（spec-03 + 实际模块文件）

- [ ] Task 13: 修复 customize.sh `abort` 未定义
  - [ ] SubTask 13.1: 阅读 [modules/nexus_prop_editor/customize.sh](file:///workspace/nexuscore/modules/nexus_prop_editor/customize.sh) 确认 abort 未 shim
  - [ ] SubTask 13.2: 在 customize.sh 顶部补 `abort() { echo "!" "$1" >&2; exit 1; }`
  - [ ] SubTask 13.3: 同步更新 spec-03 §6.4 的 customize.sh 示例
- [ ] Task 14: 评估示例模块目标（build.prop 在 Android 14+ 限制）
  - [ ] SubTask 14.1: 调研 Android 14/15/16 上 /system/build.prop 实际内容与 ro.debuggable 来源
  - [ ] SubTask 14.2: 决策：保留 build.prop 目标 + 在 README 明确限制，或改示例目标为 /system/etc/hosts
  - [ ] SubTask 14.3: 执行所选方案，更新 manifest.json description
- [ ] Task 15: 校对 customize.sh 已有 shim 与 Magisk 行为一致性
  - [ ] SubTask 15.1: 校对 `set_perm` / `set_perm_recursive` 与 Magisk 官方语义
  - [ ] SubTask 15.2: 校对 `SKIPUNZIP` 真实语义（spec-03 文档化）
  - [ ] SubTask 15.3: 补 spec-03 §6.4 注释列出所有 NexusCore shim 的函数签名

## 阶段四：定位与命名统一

- [ ] Task 16: 重新定位 NexusCore
  - [ ] SubTask 16.1: 修改 [nexuscore/README.md](file:///workspace/nexuscore/README.md) 第一段：从「用户态 Root 框架」改为「基于 Magisk/KernelSU/APatch 的用户态模块运行时」
  - [ ] SubTask 16.2: 修改 [web/index.html](file:///workspace/nexuscore/web/index.html) hero badge 与 tagline
  - [ ] SubTask 16.3: 修改 spec-01 / spec-02 / spec-03 顶部的项目定位描述
- [ ] Task 17: 统一命名（AutoVeil vs NexusCore）
  - [ ] SubTask 17.1: 全仓库 grep `AutoVeil`，确认出现位置（README / manifest homepage / 其它）
  - [ ] SubTask 17.2: 决策统一名（建议 NexusCore），更新所有引用
  - [ ] SubTask 17.3: 更新 [modules/nexus_prop_editor/manifest.json](file:///workspace/nexuscore/modules/nexus_prop_editor/manifest.json) `homepage` 字段
- [ ] Task 18: 修正 web Phase 1 进度声明
  - [ ] SubTask 18.1: 修改 [web/index.html](file:///workspace/nexuscore/web/index.html) Phase 1 状态从「● 已完成 · 工程落地成功率 90%+」改为「○ Daemon 编码未开始」
  - [ ] SubTask 18.2: 移除 `.phase.done` class

## 阶段五：文档修复

- [ ] Task 19: 修复 developer-guide.md Mount NS 误解
  - [ ] SubTask 19.1: 阅读现有 §10 常见陷阱表
  - [ ] SubTask 19.2: 删除「service.sh 在独立 Mount NS，写入会随进程退出丢失」表述
  - [ ] SubTask 19.3: 改为说明 Mount NS 只隔离 mount/unmount，/data 写入正常持久化；临时数据用 $NEXUS_TMPDIR
- [ ] Task 20: 模块格式决策（Magisk 兼容 vs 严格 DMM）
  - [ ] SubTask 20.1: 评估两种方案对生态的影响：兼容 Magisk 享受海量模块 / 坚持 DMM 需自建生态
  - [ ] SubTask 20.2: 若选「兼容」：spec-03 新增 §13「Magisk 兼容层」，定义 module.prop → DMM 自动转换
  - [ ] SubTask 20.3: 若选「严格 DMM」：spec-03 §3.2 强化 capabilities 强制校验——`WRITE_DATA_DIR`/`NETWORK_ACCESS` 从「仅记录」改为「强制拦截」（需 Daemon 实现路径白名单与 netns 隔离）
  - [ ] SubTask 20.4: 此决策需 AskUserQuestion 与用户确认

## 阶段六：交叉验证

- [ ] Task 21: 全仓库 grep 验证所有 spec 修改已落地
  - [ ] SubTask 21.1: grep `link(` 在 spec-01，确认已无跨 fs link 用法
  - [ ] SubTask 21.2: grep `u:r:untrusted_app:s0` 在 spec-01，确认已改为前缀匹配
  - [ ] SubTask 21.3: grep `0660 root:system` 在 spec-01，确认 socket 权限已修订
  - [ ] SubTask 21.4: grep `用户态 Root 框架` 全仓库，确认已统一替换
  - [ ] SubTask 21.5: grep `AutoVeil` 全仓库，确认已统一
- [ ] Task 22: spec-01 与 spec-02 proto 定义一致性校验
  - [ ] SubTask 22.1: 对比 spec-01 §10.3 proto 与 [nexus.proto](file:///workspace/nexuscore/manager/app/src/main/proto/nexus.proto)
  - [ ] SubTask 22.2: 补齐 spec-01 缺失的 Su/Reboot/UninstallFramework/ClearLogs 消息定义

# Task Dependencies

- Task 11（SU 决策）阻塞 Task 8（页面同步）与 Task 9（RPC 同步）—— 若决策为「不做」，相关页面与 RPC 需删除而非补充
- Task 12（has_update 决策）独立，可并行
- Task 14（示例目标决策）阻塞 Task 13（customize.sh 修复）—— 若改示例目标，customize.sh 内容大改
- Task 20（模块格式决策）独立但影响 spec-03 多处，建议在 Task 13 之前完成
- Task 21（验证）依赖 Task 1-20 全部完成
- Task 22（proto 一致性）依赖 Task 9 完成
- 阶段一（Task 1-7）相互独立，可并行
- 阶段二（Task 8-12）部分并行，部分串行
- 阶段三/四/五相互独立，可并行
