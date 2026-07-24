# NexusCore Spec 03 — Module SDK 与示例模块

| 字段 | 值 |
|---|---|
| Spec ID | NC-SPEC-03 |
| 版本 | 1.0.0 |
| 状态 | Approved (MVP baseline) |
| 适用阶段 | MVP（Phase 1） |
| 依赖 | [Spec 01](./spec-01-daemon.md)（Daemon 解析逻辑）、[Spec 02](./spec-02-manager.md)（Manager 安装入口） |
| 目标 | 定义模块包结构、`manifest.json` (DMM) 规范、脚本运行环境、示例模块、SDK 开发者文档 |

---

## 1. 设计原则

1. **声明式优先**：模块通过 `manifest.json` 声明所需"能力（Capabilities）"和"意图（Intents）"，Daemon 校验后授权执行，未声明的能力一律拒绝。
2. **MVP 兼容未来**：当前模块只做"文件覆盖 + 脚本执行"，但 `capabilities` / `intents` 字段让模块无需重写即可在 Phase 3（事件总线）和 Phase 4（Agent 运行时）升级。
3. **安全沙盒**：脚本在独立 Mount Namespace 内执行，环境变量注入身份信息，禁止跨模块文件互访（除非显式声明 `intents`）。
4. **零 Bootloop**：任何脚本失败只记录日志，不阻断启动。

---

## 2. 模块 ZIP 包结构规范

### 2.1 标准结构

```text
nexus_<module_id>_<version>.zip
├── manifest.json          # 核心清单 (Declarative Module Manifest) — 必需
├── system/                # 需要覆盖到 /system 的文件树 — 可选
│   ├── build.prop
│   ├── etc/
│   │   └── hosts
│   └── ...
├── post-fs-data.sh        # 早期执行脚本 (post-fs-data 阶段) — 可选
├── service.sh             # 后期执行脚本 (late_start / boot_completed 后) — 可选
├── customize.sh           # 安装时执行脚本 (Manager 安装流程触发) — 可选
├── uninstall.sh           # 卸载时执行脚本 — 可选
├── verify.sh              # 安装时环境校验脚本（返回非 0 拒绝安装）— 可选
└── META-INF/              # 安装元数据
    └── nexus_signature    # 模块作者签名指纹（MVP 可空，Phase 2 强制）
```

### 2.2 路径解析规则

| ZIP 内路径 | 落地路径（解压后） | 挂载目标 |
|---|---|---|
| `system/build.prop` | `/data/adb/nexuscore/modules/<id>/system/build.prop` | `/system/build.prop` |
| `system/etc/hosts` | `/data/adb/nexuscore/modules/<id>/system/etc/hosts` | `/system/etc/hosts` |
| `post-fs-data.sh` | `/data/adb/nexuscore/modules/<id>/post-fs-data.sh` | （脚本，不挂载） |
| `service.sh` | `/data/adb/nexuscore/modules/<id>/service.sh` | （脚本，不挂载） |

> **重要**：`system/` 下的文件会被 bind mount 或 overlay 到对应系统路径，**不直接覆盖原文件**，可随时 umount 恢复。

### 2.3 命名约束

- `id` 必须匹配正则 `^[a-z][a-z0-9_]{2,63}$`（小写字母开头，3-64 字符）
- ZIP 文件名建议：`nexus_<id>_<version>.zip`，例如 `nexus_prop_editor_1.0.0.zip`
- 同一 `id` 的模块只能存在一个实例，安装新版会自动卸载旧版

---

## 3. manifest.json 规范（DMM）

### 3.1 字段定义

| 字段 | 类型 | 必填 | 说明 |
|---|---|---|---|
| `id` | string | ✅ | 模块唯一 ID，匹配 `^[a-z][a-z0-9_]{2,63}$` |
| `name` | string | ✅ | 显示名（任意 Unicode） |
| `version` | string | ✅ | SemVer 风格，如 `1.0.0` |
| `versionCode` | integer | ❌ | 数字版本号，用于比较升级 |
| `author` | string | ✅ | 作者 |
| `description` | string | ❌ | 一句话描述 |
| `min_nexus_version` | string | ✅ | 最低支持的 NexusCore 版本，如 `1.0` |
| `priority` | integer | ❌ | 加载优先级，**默认 0**。值越大越后挂载（覆盖前者）。范围 [-1000, 1000] |
| `enabled` | boolean | ❌ | 默认 `true` |
| `capabilities` | string[] | ❌ | 所需能力白名单，见 §3.2 |
| `intents` | Intent[] | ❌ | 订阅的意图事件，见 §3.3 |
| `permissions` | string[] | ❌ | 需要的额外权限（如 `ROOT_SHELL`）— MVP 仅记录 |
| `homepage` | string | ❌ | 项目主页 URL |
| `support` | string | ❌ | 支持渠道 URL |
| `donate` | string | ❌ | 捐赠链接 |

### 3.2 Capabilities 能力白名单

> **整改：未声明的能力将被拒绝执行，提升安全性。**

| 能力 | 含义 | MVP 是否强制校验 |
|---|---|---|
| `EXECUTE_SHELL` | 允许执行 `post-fs-data.sh` / `service.sh` | ✅ |
| `MODIFY_SYSTEM_PROPS` | 允许通过 `setprop` 修改系统属性 | ✅ |
| `MOUNT_FILESYSTEM` | 允许 `system/` 目录被挂载到系统分区 | ✅ |
| `WRITE_DATA_DIR` | 允许写入 `/data/adb/nexuscore/<id>/` 之外的路径 | ⚠️ MVP 仅记录 |
| `NETWORK_ACCESS` | 允许脚本访问网络（默认禁） | ⚠️ MVP 仅记录 |
| `ACCESS_OTHER_MODULES` | 允许读取其它模块目录 | ❌ Phase 3 |
| `REGISTER_AGENT` | 注册为 Agent 模块（Phase 4） | ❌ Phase 4 |

**校验时机**：Daemon 在 `parseManifest` 后立即校验；若脚本试图执行未声明的能力（例如未声明 `EXECUTE_SHELL` 却提供了 `service.sh`），Daemon 会跳过该脚本并记录 WARN。

### 3.3 Intents 意图声明

```jsonc
"intents": [
  { "action": "SYSTEM_BOOT_COMPLETED", "priority": 10 },
  { "action": "PACKAGE_ADDED", "priority": 5, "filter": { "package": "com.android.*" } }
]
```

| 字段 | 类型 | 说明 |
|---|---|---|
| `action` | string | 意图名，大写下划线 |
| `priority` | integer | 多模块订阅同一意图时的派发顺序 |
| `filter` | object | MVP 仅记录，Phase 3 由事件总线消费 |

**MVP 已识别的 Intent Action**：
- `SYSTEM_BOOT_COMPLETED`
- `DAEMON_READY`
- `MODULE_LOADED`（payload: `module_id`）
- `MODULE_UNLOADED`（payload: `module_id`）
- `SCRIPT_DONE`（payload: `script`, `code`）

---

## 4. JSON Schema 定义

### 4.1 `sdk/nexus_module.schema.json`

```json
{
  "$schema": "https://json-schema.org/draft/2020-12/schema",
  "$id": "https://nexuscore.dev/schema/module-manifest-v1.json",
  "title": "NexusCore Module Manifest (DMM)",
  "type": "object",
  "required": ["id", "name", "version", "author", "min_nexus_version"],
  "additionalProperties": false,
  "properties": {
    "id": {
      "type": "string",
      "pattern": "^[a-z][a-z0-9_]{2,63}$",
      "description": "模块唯一 ID"
    },
    "name": { "type": "string", "minLength": 1, "maxLength": 64 },
    "version": { "type": "string", "pattern": "^\\d+\\.\\d+\\.\\d+(-[0-9A-Za-z.-]+)?$" },
    "versionCode": { "type": "integer", "minimum": 1 },
    "author": { "type": "string", "minLength": 1 },
    "description": { "type": "string", "maxLength": 256 },
    "min_nexus_version": { "type": "string", "pattern": "^\\d+\\.\\d+" },
    "priority": { "type": "integer", "minimum": -1000, "maximum": 1000 },
    "enabled": { "type": "boolean" },
    "capabilities": {
      "type": "array",
      "items": {
        "type": "string",
        "enum": [
          "EXECUTE_SHELL",
          "MODIFY_SYSTEM_PROPS",
          "MOUNT_FILESYSTEM",
          "WRITE_DATA_DIR",
          "NETWORK_ACCESS",
          "ACCESS_OTHER_MODULES",
          "REGISTER_AGENT"
        ]
      },
      "uniqueItems": true
    },
    "intents": {
      "type": "array",
      "items": {
        "type": "object",
        "required": ["action"],
        "additionalProperties": false,
        "properties": {
          "action": { "type": "string", "minLength": 1 },
          "priority": { "type": "integer", "minimum": 0, "default": 0 },
          "filter": { "type": "object" }
        }
      }
    },
    "permissions": {
      "type": "array",
      "items": { "type": "string" },
      "uniqueItems": true
    },
    "homepage": { "type": "string", "format": "uri" },
    "support": { "type": "string", "format": "uri" },
    "donate": { "type": "string", "format": "uri" }
  }
}
```

### 4.2 校验工具

```bash
# 使用 ajv-cli 校验（开发者本地）
ajv validate -s nexus_module.schema.json -d manifest.json --strict=false
```

Daemon 在解析时使用 C++ 内嵌的轻量校验（`nlohmann::json` + 手写正则），不引入完整 JSON Schema 库以节省体积。MVP 仅校验：`required`、`id` 正则、`capabilities` 枚举、`priority` 范围。

---

## 5. 脚本执行环境规范

### 5.1 执行阶段与时机

| 脚本 | 执行阶段 | 时机 | 能力要求 | Mount Namespace |
|---|---|---|---|---|
| `post-fs-data.sh` | `POST_FS_DATA` | `/data` 挂载后，zygote 启动前 | `EXECUTE_SHELL` | 独立 NS（不污染全局） |
| `service.sh` | `LATE_START` | `sys.boot_completed=1` 后 | `EXECUTE_SHELL` | 独立 NS |
| `customize.sh` | （安装时） | Manager 触发安装时 | `EXECUTE_SHELL` | 共享 NS（需写入模块目录） |
| `uninstall.sh` | （卸载时） | Manager 触发卸载时 | `EXECUTE_SHELL` | 共享 NS |
| `verify.sh` | （安装时） | `customize.sh` 之前 | 无 | 共享 NS |

### 5.2 注入的环境变量

| 变量 | 含义 | 示例 |
|---|---|---|
| `NEXUS_MODULE_PATH` | 模块解压根目录 | `/data/adb/nexuscore/modules/nexus_prop_editor` |
| `NEXUS_MODULE_ID` | 模块 ID | `nexus_prop_editor` |
| `NEXUS_MODULE_VERSION` | 模块版本 | `1.0.0` |
| `NEXUS_VERSION` | Daemon 版本 | `1.0.0` |
| `NEXUS_BOOT_STAGE` | 当前阶段 | `post-fs-data` / `late_start` / `install` / `uninstall` |
| `NEXUS_ROOT_PROVIDER` | Root 来源 | `magisk` / `kernelsu` / `apatch` |
| `NEXUS_API_LEVEL` | Android API | `34` |
| `NEXUS_ARCH` | 设备架构 | `arm64` |
| `NEXUS_OVERLAY_BASE` | overlay 根目录 | `/data/adb/nexuscore/overlay` |
| `NEXUS_TMPDIR` | 模块临时目录 | `/data/adb/nexuscore/tmp/<id>` |

### 5.3 脚本约束

1. **必须使用 `#!/system/bin/sh`**（POSIX sh，不支持 bashism）
2. 单脚本最长执行时间 **120 秒**，超时 kill
3. **退出码语义**：
   - `0` = 成功
   - `1` = 一般失败（仅记录日志，不阻断）
   - `2` = 严重失败（Daemon 标记模块为 disabled，下次启动不加载）
   - 其他 = 警告
4. 脚本必须可重入（Daemon 可能多次调用，例如安全模式重试）
5. **禁止**：fork bomb、`reboot`、卸载系统关键分区、修改 `/data/adb/nexuscore/bin/nexusd`

### 5.4 输出与日志

脚本 `stdout` / `stderr` 被 Daemon 重定向到 `/data/adb/nexuscore/logs/<id>.log`，同时通过 `EVENT_SCRIPT_DONE` 事件广播给订阅者（Manager Logs 页可看到）。

---

## 6. NexusProp Editor 示例模块

### 6.1 功能说明

- **目标**：允许用户修改 `build.prop` 中的 `ro.debuggable` 和 `ro.secure` 属性
- **实现方式**（按 Prompt 1 要求）：
  - 不在 `system/` 直接放文件，而是通过 `customize.sh` 在安装时读取真实 `/system/build.prop`，修改指定属性，生成新文件放入模块的 `system/` 树
  - `service.sh` 在开机后通过文件写入记录修改日志
- **教学价值**：完整覆盖 manifest、capabilities、customize、service 四个核心场景

### 6.2 文件清单

```
modules/nexus_prop_editor/
├── manifest.json
├── customize.sh
├── post-fs-data.sh
├── service.sh
├── uninstall.sh
├── verify.sh
├── system/                   # 由 customize.sh 在安装时填充
└── build.sh                  # 打包脚本
```

### 6.3 `manifest.json`

```json
{
  "id": "nexus_prop_editor",
  "name": "NexusProp Editor",
  "version": "1.0.0",
  "versionCode": 1,
  "author": "NexusCore Team",
  "description": "安全修改 build.prop 中的 ro.debuggable / ro.secure 属性",
  "min_nexus_version": "1.0",
  "priority": 100,
  "enabled": true,
  "capabilities": [
    "EXECUTE_SHELL",
    "MODIFY_SYSTEM_PROPS",
    "MOUNT_FILESYSTEM"
  ],
  "intents": [
    { "action": "SYSTEM_BOOT_COMPLETED", "priority": 10 }
  ],
  "homepage": "https://github.com/AceGuru-mjh/AutoVeil"
}
```

### 6.4 `customize.sh`（安装时执行）

```sh
#!/system/bin/sh
# NexusProp Editor 安装脚本
# 读取真实 /system/build.prop，生成修改版放入模块 system/ 目录

SKIPUNZIP=0

ui_print "- NexusProp Editor 安装开始"
ui_print "- 模块路径: $NEXUS_MODULE_PATH"

PROP_FILE="$MODPATH/system/build.prop"
mkdir -p "$MODPATH/system"

# ====== 用户配置（可在此处编辑） ======
SET_DEBUGGABLE=0      # 1=可调试，0=不可调试（推荐 0 保持安全）
SET_SECURE=1          # 1=安全，0=不安全（推荐 1）
# ====================================

if [ ! -f "/system/build.prop" ]; then
    ui_print "! /system/build.prop 不存在，可能为动态分区设备"
    ui_print "! 尝试从 /system_root/system/build.prop 读取"
    SRC_PROP="/system_root/system/build.prop"
else
    SRC_PROP="/system/build.prop"
fi

if [ ! -f "$SRC_PROP" ]; then
    ui_print "! 无法定位 build.prop，安装中止"
    abort "build.prop not found"
fi

ui_print "- 源文件: $SRC_PROP"
ui_print "- 目标值: ro.debuggable=$SET_DEBUGGABLE, ro.secure=$SET_SECURE"

# 复制原始 build.prop
cp "$SRC_PROP" "$PROP_FILE"
chmod 644 "$PROP_FILE"

# 用 sed 修改指定属性
# 注意：ro.* 属性在系统启动后无法 setprop 修改，必须在 build.prop 文件层覆盖
if grep -q "^ro.debuggable=" "$PROP_FILE"; then
    sed -i "s|^ro.debuggable=.*|ro.debuggable=$SET_DEBUGGABLE|" "$PROP_FILE"
else
    echo "ro.debuggable=$SET_DEBUGGABLE" >> "$PROP_FILE"
fi

if grep -q "^ro.secure=" "$PROP_FILE"; then
    sed -i "s|^ro.secure=.*|ro.secure=$SET_SECURE|" "$PROP_FILE"
else
    echo "ro.secure=$SET_SECURE" >> "$PROP_FILE"
fi

ui_print "- 修改完成"
ui_print "- 文件大小: $(wc -c < "$PROP_FILE") bytes"

# 记录安装日志（写入模块目录，service.sh 启动后会上报）
{
    echo "$(date) installed by NexusCore"
    echo "  source: $SRC_PROP"
    echo "  ro.debuggable=$SET_DEBUGGABLE"
    echo "  ro.secure=$SET_SECURE"
} > "$MODPATH/install.log"

ui_print "- 重启后生效"
set_perm_recursive "$MODPATH/system" 0 0 0755 0644
```

> **说明**：`MODPATH` 等变量是 NexusCore 在执行 `customize.sh` 时注入的别名，等价于 `$NEXUS_MODULE_PATH`。为兼容 Magisk 习惯，Daemon 也注入 `MODPATH`、`TMPDIR`、`ZIPFILE`。

### 6.5 `post-fs-data.sh`（早期执行）

```sh
#!/system/bin/sh
# NexusProp Editor post-fs-data 阶段
# 此阶段 zygote 未启动，build.prop 已被 Daemon bind mount

# 仅记录一条日志，证明脚本被执行
echo "$(date) post-fs-data: build.prop overlay active" >> "$NEXUS_MODULE_PATH/runtime.log"

# 注意：此阶段不要 setprop，因为 ro.* 已被 build.prop 覆盖
# 系统会自动从新 build.prop 加载属性

exit 0
```

### 6.6 `service.sh`（boot_completed 后）

```sh
#!/system/bin/sh
# NexusProp Editor late_start 阶段
# 验证属性是否生效，并记录日志

LOG="$NEXUS_MODULE_PATH/runtime.log"

{
    echo "===== $(date) service.sh ====="
    echo "实际 ro.debuggable = $(getprop ro.debuggable)"
    echo "实际 ro.secure      = $(getprop ro.secure)"
    echo "build.prop hash     = $(md5sum /system/build.prop 2>/dev/null | awk '{print $1}')"
    echo "================================"
} >> "$LOG"

# 通过 Daemon 事件总线报告（如果 NexusCore 提供了 cli 工具）
if [ -x "/data/adb/nexuscore/bin/nexuscli" ]; then
    /data/adb/nexuscore/bin/nexuscli emit EVENT_MODULE_REPORT \
        --module="$NEXUS_MODULE_ID" \
        --data="debuggable=$(getprop ro.debuggable),secure=$(getprop ro.secure)" \
        2>/dev/null || true
fi

exit 0
```

### 6.7 `uninstall.sh`

```sh
#!/system/bin/sh
# NexusProp Editor 卸载脚本
# Daemon 会自动 umount system/build.prop，本脚本只清理运行时数据

LOG="/data/adb/nexuscore/logs/$NEXUS_MODULE_ID.uninstall.log"
{
    echo "$(date) uninstalling $NEXUS_MODULE_ID"
    echo "runtime.log tail:"
    tail -n 20 "$NEXUS_MODULE_PATH/runtime.log" 2>/dev/null
} > "$LOG"

# 不需要恢复 /system/build.prop —— Daemon umount 后原文件自然恢复
exit 0
```

### 6.8 `verify.sh`（安装前环境校验）

```sh
#!/system/bin/sh
# 校验当前环境是否支持本模块

# 1. 必须是 NexusCore 1.0+
if [ -z "$NEXUS_VERSION" ]; then
    echo "! 不在 NexusCore 环境中" >&2
    exit 1
fi

# 2. build.prop 必须可读
if [ ! -r "/system/build.prop" ] && [ ! -r "/system_root/system/build.prop" ]; then
    echo "! 找不到可读的 build.prop" >&2
    exit 1
fi

# 3. Android API 必须 >= 30（MVP 目标 34，向下兼容到 30）
API=$(getprop ro.build.version.sdk)
if [ "$API" -lt 30 ] 2>/dev/null; then
    echo "! Android API $API 过低，需要 >= 30" >&2
    exit 1
fi

exit 0
```

### 6.9 `build.sh`（打包脚本）

```sh
#!/usr/bin/env bash
# 打包 NexusProp Editor 模块 ZIP
set -euo pipefail

MODULE_ID="nexus_prop_editor"
VERSION="1.0.0"
STAGE="${1:-build}"
OUT_DIR="dist"
OUT="$OUT_DIR/nexus_${MODULE_ID}_${VERSION}.zip"

rm -rf "$OUT_DIR"
mkdir -p "$OUT_DIR"

# 临时打包目录
TMP="$(mktemp -d)"
trap "rm -rf $TMP" EXIT

cp -r "$MODULE_ID"/* "$TMP/" 2>/dev/null || true
cp -r "modules/$MODULE_ID"/* "$TMP/" 2>/dev/null || true

# 校验 manifest
if [ ! -f "$TMP/manifest.json" ]; then
    echo "! manifest.json missing" >&2
    exit 1
fi

# 校验所有脚本可执行权限
chmod +x "$TMP"/*.sh 2>/dev/null || true

# 打包（不能包含外层目录）
( cd "$TMP" && zip -r9 "$OLDPWD/$OUT" . -x "*.DS_Store" "*/.git*" )

echo "✓ Built: $OUT"
unzip -l "$OUT"
```

---

## 7. SDK 开发者文档

> 本节是面向第三方模块开发者的 Markdown 指南，独立成文：`sdk/docs/developer-guide.md`。

### 7.1 快速开始

1. 创建模块目录：
   ```bash
   mkdir -p my_module/system
   ```

2. 编写最小 `manifest.json`：
   ```json
   {
     "id": "my_module",
     "name": "My First Module",
     "version": "0.1.0",
     "author": "you",
     "min_nexus_version": "1.0",
     "capabilities": ["EXECUTE_SHELL"]
   }
   ```

3. 加入一个脚本 `service.sh`：
   ```sh
   #!/system/bin/sh
   echo "Hello from $NEXUS_MODULE_ID" > /data/local/tmp/hello.txt
   ```

4. 打包：
   ```bash
   cd my_module && zip -r9 ../my_module.zip .
   ```

5. 通过 NexusManager "从本地 ZIP 安装"，重启后生效。

### 7.2 capabilities 速查

| 想做的事 | 必须声明 |
|---|---|
| 提供任何 `.sh` 脚本 | `EXECUTE_SHELL` |
| 让 `system/` 文件被覆盖挂载 | `MOUNT_FILESYSTEM` |
| 在脚本里 `setprop` | `MODIFY_SYSTEM_PROPS` |
| 写 `/data/adb/nexuscore/<id>/` 之外的路径 | `WRITE_DATA_DIR`（MVP 仅记录） |
| 访问网络 | `NETWORK_ACCESS`（MVP 仅记录） |

> **未声明能力的后果**：Daemon 会拒绝执行对应行为，并在日志中记录 `CAPABILITY_DENIED`。模块可能"看起来安装成功但什么都没做"，请务必准确声明。

### 7.3 priority 与冲突

- `priority` 范围 `[-1000, 1000]`，默认 `0`
- **值越大越后挂载**，因此后挂载者覆盖前者
- 推荐约定：
  - `-100 ~ -1`：基础库类模块（被覆盖）
  - `0`（默认）：普通模块
  - `1 ~ 100`：调整类模块（覆盖默认）
  - `100+`：用户主动覆盖类（最高优先级）

> **MVP 冲突处理**：同 target 路径，高 priority 直接覆盖低 priority（最终生效一个）。Phase 3 引入事件总线后改为"内存缝合"（多个模块对同一属性的修改叠加）。

### 7.4 脚本调试

1. **本地试跑**：把 `NEXUS_MODULE_PATH` 等变量手动 export 后执行：
   ```bash
   export NEXUS_MODULE_PATH=/tmp/test_module
   export NEXUS_MODULE_ID=test
   export NEXUS_BOOT_STAGE=late_start
   sh service.sh
   ```

2. **查看日志**：
   ```bash
   adb shell cat /data/adb/nexuscore/logs/<id>.log
   ```

3. **Manager 实时日志页**：直接看 Logs 页，过滤 tag 为模块 ID。

### 7.5 常见陷阱

| 陷阱 | 解决 |
|---|---|
| `setprop ro.xxx` 报 Read-only | `ro.*` 不能 setprop，必须通过 `system/build.prop` 覆盖 |
| 脚本在 `post-fs-data` 阶段访问网络 | 此阶段网络未就绪，放到 `service.sh` |
| `service.sh` 修改的文件重启后消失 | `service.sh` 在独立 Mount NS，写入会随进程退出丢失。持久化数据写到 `/data/adb/nexuscore/<id>/` |
| 多个模块覆盖同一文件冲突 | 用 `priority` 调整顺序；Phase 3 后用事件总线 |
| `customize.sh` 里 `$MODPATH` 为空 | 必须由 Daemon 调用，不要手动执行 |

### 7.6 升级到 Phase 4 Agent 模块

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

## 8. 安装流程（Daemon 端）

```
1. Manager 调用 InstallModuleRequest(local_path="/data/local/tmp/xxx.zip")
2. Daemon 收到请求 → 校验调用者凭证（Spec 01 §10.2）
3. 解压 ZIP 到临时目录 /data/adb/nexuscore/tmp/<id>/
4. 解析 manifest.json
   ├─ 校验 JSON Schema（required 字段、id 正则、capabilities 枚举）
   ├─ 校验 min_nexus_version
   └─ 若同 id 已存在 → 标记为升级，先备份旧目录
5. 执行 verify.sh（若存在）
   └─ 返回非 0 → 拒绝安装，清理临时目录，返回错误码
6. 执行 customize.sh（若存在）
   └─ 在共享 Mount NS 执行，可写入模块目录
7. 移动临时目录到 /data/adb/nexuscore/modules/<id>/
8. 写入 install.log
9. 发布 EVENT_MODULE_LOADED 事件
10. 返回 InstallModuleResponse(id, need_reboot=true)
```

**回滚**：第 5、6 步任意失败，自动恢复第 4 步备份的旧模块目录，确保不会因为安装失败导致下次启动异常。

---

## 9. 验收标准

- [ ] `manifest.json` JSON Schema 可被 `ajv` 校验通过
- [ ] NexusProp Editor ZIP 可被 Daemon 正确安装
- [ ] 安装后 `customize.sh` 生成的 `system/build.prop` 被 bind mount 到 `/system/build.prop`
- [ ] `getprop ro.debuggable` 反映修改后的值
- [ ] `service.sh` 在 boot_completed 后执行，`runtime.log` 有记录
- [ ] 卸载后 `/system/build.prop` 恢复原始内容
- [ ] `verify.sh` 在低 API 设备返回非 0，Daemon 拒绝安装
- [ ] 同 id 模块二次安装时自动卸载旧版
- [ ] 未声明 `EXECUTE_SHELL` 的模块，其 `service.sh` 被跳过且日志记录 `CAPABILITY_DENIED`

---

## 10. 风险与缓解

| 风险 | 缓解 |
|---|---|
| `customize.sh` 误改 `/system` 真实文件 | customize 在共享 NS 但脚本写入路径限定为 `$MODPATH`；越界写操作由 SELinux 拦截 |
| `priority` 设置不当导致覆盖链错乱 | 文档明确约定范围；Phase 3 事件总线解决根本问题 |
| 第三方模块带恶意 `service.sh` | MVP 信任签名（`META-INF/nexus_signature`，Phase 2 强制）；Manager 显示 capabilities 警告 |
| `build.prop` 在 EROFS 设备上不可 bind | Daemon 检测目标存在性，不存在则跳过并告警（Spec 01 §6.5） |

---

## 11. 与其它 Spec 的依赖

- manifest.json 解析逻辑见 [Spec 01 §7.2](./spec-01-daemon.md#72-modulemanifesth--dmm-解析)
- Manager 安装入口见 [Spec 02 §6.4](./spec-02-manager.md#64-uipagesmodulespagekt)
- 事件总线 Intent 列表见 [Spec 01 §8.2](./spec-01-daemon.md#82-eventi_event_bush)
