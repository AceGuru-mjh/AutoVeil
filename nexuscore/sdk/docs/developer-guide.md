# NexusCore Module SDK 开发者指南

> 适用版本：NexusCore 1.0 (MVP)
> 最后更新：2026-07

本指南面向第三方模块开发者，介绍如何打包、声明能力、编写脚本、调试与发布 NexusCore 模块。

---

## 目录

1. [快速开始](#1-快速开始)
2. [模块包结构](#2-模块包结构)
3. [manifest.json 字段详解](#3-manifestjson-字段详解)
4. [Capabilities 能力声明](#4-capabilities-能力声明)
5. [Intents 意图订阅](#5-intents-意图订阅)
6. [priority 与冲突解决](#6-priority-与冲突解决)
7. [脚本编写规范](#7-脚本编写规范)
8. [环境变量参考](#8-环境变量参考)
9. [调试技巧](#9-调试技巧)
10. [常见陷阱](#10-常见陷阱)
11. [升级到 Agent 模块（Phase 4）](#11-升级到-agent-模块phase-4)
12. [发布清单](#12-发布清单)

---

## 1. 快速开始

```bash
# 1. 创建模块骨架
mkdir -p my_module/system
cd my_module

# 2. 编写最小 manifest.json
cat > manifest.json <<'EOF'
{
  "id": "my_module",
  "name": "My First Module",
  "version": "0.1.0",
  "author": "you",
  "min_nexus_version": "1.0",
  "capabilities": ["EXECUTE_SHELL"]
}
EOF

# 3. 加一个 service.sh
cat > service.sh <<'EOF'
#!/system/bin/sh
echo "Hello from $NEXUS_MODULE_ID" > /data/local/tmp/hello.txt
EOF
chmod +x service.sh

# 4. 打包
zip -r9 ../my_module.zip .

# 5. 通过 NexusManager "从本地 ZIP 安装"，重启后生效
```

---

## 2. 模块包结构

```text
nexus_<id>_<version>.zip
├── manifest.json          # 必需
├── system/                # 可选：覆盖到 /system
├── post-fs-data.sh        # 可选：早期脚本
├── service.sh             # 可选：后期脚本
├── customize.sh           # 可选：安装时脚本
├── uninstall.sh           # 可选：卸载时脚本
├── verify.sh              # 可选：安装前环境校验
└── META-INF/
    └── nexus_signature    # 可选：作者签名（Phase 2 强制）
```

**路径映射**：ZIP 内 `system/build.prop` → 挂载到 `/system/build.prop`，原文件保持不变（bind mount），卸载即恢复。

---

## 3. manifest.json 字段详解

| 字段 | 类型 | 必填 | 默认 | 说明 |
|---|---|---|---|---|
| `id` | string | ✅ | — | 唯一 ID，正则 `^[a-z][a-z0-9_]{2,63}$` |
| `name` | string | ✅ | — | 显示名 |
| `version` | string | ✅ | — | SemVer，如 `1.0.0` |
| `versionCode` | int | ❌ | — | 数字版本号，用于比较升级 |
| `author` | string | ✅ | — | 作者 |
| `description` | string | ❌ | `""` | 一句话描述 |
| `min_nexus_version` | string | ✅ | — | 最低 NexusCore 版本 |
| `priority` | int | ❌ | `0` | 范围 [-1000, 1000]，越大越后挂载（覆盖前者） |
| `enabled` | bool | ❌ | `true` | 默认是否启用 |
| `capabilities` | string[] | ❌ | `[]` | 所需能力，见 §4 |
| `intents` | object[] | ❌ | `[]` | 订阅的意图，见 §5 |
| `permissions` | string[] | ❌ | `[]` | 额外权限（MVP 仅记录） |
| `homepage` | string | ❌ | — | 项目主页 |
| `support` | string | ❌ | — | 支持渠道 |
| `donate` | string | ❌ | — | 捐赠链接 |

完整 JSON Schema 见 [`nexus_module.schema.json`](../nexus_module.schema.json)。

---

## 4. Capabilities 能力声明

> **核心安全机制**：未声明的能力一律拒绝执行。

| 能力 | 含义 | MVP 强制 |
|---|---|---|
| `EXECUTE_SHELL` | 允许执行任何 `.sh` 脚本 | ✅ |
| `MODIFY_SYSTEM_PROPS` | 允许 `setprop` 修改系统属性 | ✅ |
| `MOUNT_FILESYSTEM` | 允许 `system/` 目录被挂载 | ✅ |
| `WRITE_DATA_DIR` | 允许写入模块目录之外的路径 | ⚠️ 仅记录 |
| `NETWORK_ACCESS` | 允许脚本访问网络 | ⚠️ 仅记录 |
| `ACCESS_OTHER_MODULES` | 允许读取其它模块目录 | ❌ Phase 3 |
| `REGISTER_AGENT` | 注册为 Agent 模块 | ❌ Phase 4 |

**校验时机**：Daemon 在 `parseManifest` 后立即校验。若脚本试图执行未声明的能力，Daemon 跳过并记录 `CAPABILITY_DENIED`。

---

## 5. Intents 意图订阅

```jsonc
"intents": [
  { "action": "SYSTEM_BOOT_COMPLETED", "priority": 10 },
  { "action": "PACKAGE_ADDED", "priority": 5, "filter": { "package": "com.android.*" } }
]
```

**MVP 已识别的 Action**：
- `SYSTEM_BOOT_COMPLETED`
- `DAEMON_READY`
- `MODULE_LOADED` (payload: `module_id`)
- `MODULE_UNLOADED` (payload: `module_id`)
- `SCRIPT_DONE` (payload: `script`, `code`)

> MVP 阶段 intents 仅记录订阅关系，不真正派发。Phase 3 事件总线启用后才生效。

---

## 6. priority 与冲突解决

- 范围 `[-1000, 1000]`，默认 `0`
- **值越大越后挂载**，因此后挂载者覆盖前者
- 推荐约定：

| 范围 | 用途 |
|---|---|
| `-100 ~ -1` | 基础库类模块（被覆盖） |
| `0`（默认） | 普通模块 |
| `1 ~ 100` | 调整类模块（覆盖默认） |
| `100+` | 用户主动覆盖类（最高优先级） |

**MVP 冲突处理**：同 target 路径，高 priority 直接覆盖低 priority，最终只生效一个。
**Phase 3 升级**：事件总线启用"内存缝合"，多模块对同一属性的修改叠加。

---

## 7. 脚本编写规范

### 7.1 执行阶段

| 脚本 | 阶段 | 时机 | 能力要求 | Mount NS |
|---|---|---|---|---|
| `post-fs-data.sh` | POST_FS_DATA | /data 挂载后，zygote 启动前 | `EXECUTE_SHELL` | 独立 |
| `service.sh` | LATE_START | boot_completed 后 | `EXECUTE_SHELL` | 独立 |
| `customize.sh` | 安装时 | Manager 触发安装时 | `EXECUTE_SHELL` | 共享 |
| `uninstall.sh` | 卸载时 | Manager 触发卸载时 | `EXECUTE_SHELL` | 共享 |
| `verify.sh` | 安装时 | `customize.sh` 之前 | 无 | 共享 |

### 7.2 约束

1. **必须使用 `#!/system/bin/sh`**（POSIX sh，不支持 bashism）
2. 单脚本最长执行 **120 秒**，超时 kill
3. **退出码**：
   - `0` 成功
   - `1` 一般失败（仅记录，不阻断）
   - `2` 严重失败（Daemon 标记模块 disabled，下次启动不加载）
4. 脚本必须可重入
5. **禁止**：fork bomb、`reboot`、卸载系统分区、修改 `/data/adb/nexuscore/bin/nexusd`

### 7.3 输出与日志

- `stdout` / `stderr` → `/data/adb/nexuscore/logs/<id>.log`
- 同时通过 `EVENT_SCRIPT_DONE` 事件广播给 Manager Logs 页

---

## 8. 环境变量参考

| 变量 | 示例 |
|---|---|
| `NEXUS_MODULE_PATH` | `/data/adb/nexuscore/modules/my_module` |
| `NEXUS_MODULE_ID` | `my_module` |
| `NEXUS_MODULE_VERSION` | `0.1.0` |
| `NEXUS_VERSION` | `1.0.0` |
| `NEXUS_BOOT_STAGE` | `post-fs-data` / `late_start` / `install` / `uninstall` |
| `NEXUS_ROOT_PROVIDER` | `magisk` / `kernelsu` / `apatch` |
| `NEXUS_API_LEVEL` | `34` |
| `NEXUS_ARCH` | `arm64` |
| `NEXUS_OVERLAY_BASE` | `/data/adb/nexuscore/overlay` |
| `NEXUS_TMPDIR` | `/data/adb/nexuscore/tmp/my_module` |

---

## 9. 调试技巧

### 9.1 本地试跑

```bash
export NEXUS_MODULE_PATH=/tmp/test_module
export NEXUS_MODULE_ID=test
export NEXUS_MODULE_VERSION=0.1.0
export NEXUS_BOOT_STAGE=late_start
mkdir -p $NEXUS_MODULE_PATH
sh service.sh
```

### 9.2 查看日志

```bash
adb shell cat /data/adb/nexuscore/logs/my_module.log
adb shell tail -f /data/adb/nexuscore/logs/my_module.log
```

### 9.3 Manager 实时日志

打开 NexusManager → 日志页，过滤 tag 为模块 ID。

### 9.4 检查挂载状态

```bash
adb shell mount | grep my_module
adb shell cat /proc/mounts | grep nexus
```

---

## 10. 常见陷阱

| 陷阱 | 解决 |
|---|---|
| `setprop ro.xxx` 报 Read-only | `ro.*` 不能 setprop，必须通过 `system/build.prop` 覆盖 |
| 脚本在 `post-fs-data` 访问网络 | 此阶段网络未就绪，放到 `service.sh` |
| `service.sh` 修改的文件重启后消失 | `service.sh` 在独立 Mount NS，写入会随进程退出丢失。持久化写到 `/data/adb/nexuscore/<id>/` |
| 多模块覆盖同一文件冲突 | 用 `priority` 调整顺序；Phase 3 后用事件总线 |
| `customize.sh` 里 `$NEXUS_MODULE_PATH` 为空 | 必须由 Daemon 调用，不要手动执行 |
| 模块"安装成功但什么都没做" | 检查 `capabilities` 是否声明完整，看日志 `CAPABILITY_DENIED` |
| EROFS 设备 `/system/x` 不存在 | bind mount 无法在 EROFS 上创建新文件，Daemon 会跳过并告警 |

---

## 11. 升级到 Agent 模块（Phase 4）

未来想把现有模块升级为 Agent 模块，只需在 `manifest.json` 添加：

```json
{
  "capabilities": ["REGISTER_AGENT"],
  "intents": [
    { "action": "SYSTEM_THERMAL_HIGH", "priority": 5 },
    { "action": "MEMORY_PRESSURE", "priority": 5 }
  ]
}
```

并提供 `agent.lua`（Phase 4 引入 Lua 运行时）。**现有的 `system/` 覆盖和 `.sh` 脚本无需改动**。

---

## 12. 发布清单

发布模块前，确认：

- [ ] `id` 唯一且符合命名规范
- [ ] `version` 遵循 SemVer
- [ ] `capabilities` 仅声明实际使用的能力（最小权限原则）
- [ ] `min_nexus_version` 准确
- [ ] 所有脚本以 `#!/system/bin/sh` 开头
- [ ] `verify.sh` 校验 API level 与所需文件
- [ ] `customize.sh` 不依赖外部网络
- [ ] `uninstall.sh` 清理运行时数据
- [ ] 在 Android 14/15 真机验证至少 3 次冷启动无异常
- [ ] 打包后 `unzip -l` 检查文件结构
- [ ] （推荐）`META-INF/nexus_signature` 签名（Phase 2 强制）

---

## 13. 从 Magisk 模块迁移

NexusCore 采用自有命名体系（`NEXUS_` 前缀变量、`nexus_` 前缀函数），**不内置 Magisk 兼容层**。迁移时可参考以下步骤：

### 13.1 清单文件迁移

| Magisk | NexusCore |
|---|---|
| `module.prop` | `manifest.json` |
| `id` | `id` |
| `name` | `name` |
| `version` / `versionCode` | `version` / `versionCode` |
| `author` | `author` |
| `description` | `description` |
| （无对应） | `capabilities`（必须声明） |
| （无对应） | `priority`（模块加载顺序） |
| （无对应） | `min_nexus_version` |

### 13.2 环境变量/函数迁移

| Magisk | NexusCore |
|---|---|
| `$MODPATH` | `$NEXUS_MODULE_PATH` |
| `$TMPDIR` | `$NEXUS_TMPDIR` |
| `$API` | `$NEXUS_API_LEVEL` |
| `ui_print` | `nexus_log` |
| `abort` | `nexus_abort` |
| `set_perm` | `nexus_set_perm` |
| `set_perm_recursive` | `nexus_set_perm_recursive` |
| `SKIPUNZIP` | （不适用，NexusCore 统一解压） |

### 13.3 快速替换命令

```bash
# 在模块目录下执行
sed -i 's/\$MODPATH/\$NEXUS_MODULE_PATH/g' *.sh
sed -i 's/ui_print /nexus_log /g' *.sh
sed -i 's/abort /nexus_abort /g' *.sh
sed -i 's/set_perm /nexus_set_perm /g' *.sh
sed -i 's/set_perm_recursive /nexus_set_perm_recursive /g' *.sh
```

> 注意：`system.prop`、`sepolicy.rule`、`zygisk/` 等 Magisk 特有机制在 NexusCore 中不适用。属性修改请通过 `customize.sh` + `system/build.prop` 覆盖实现。

---

## 参考

- [Spec 01 — Daemon](../../specs/spec-01-daemon.md)
- [Spec 03 — Module SDK](../../specs/spec-03-module-sdk.md)
- [JSON Schema](../nexus_module.schema.json)
- [示例模块 NexusProp Editor](../../modules/nexus_prop_editor/)
