#!/system/bin/sh
# NexusCore Daemon 安装脚本（Magisk module customize.sh）
#
# 这个脚本由 Magisk/KSU/APatch 在系统启动时调用（作为模块的 customize.sh），
# 负责：
# 1. 把 nexusd 二进制放到 /data/adb/nexuscore/bin/
# 2. 注册 init.rc 启动 nexusd
# 3. 解析 com.nexus.manager 的 UID 写入 /data/adb/nexuscore/manager_uid
#    （daemon 启动时读取此文件，给 socket chown，避免 catch-22）

SKIPUNZIP=0
ui_print "- NexusCore Daemon 安装开始"

# ===== 路径 =====
NEXUSCORE_DIR=/data/adb/nexuscore
BIN_DIR=$NEXUSCORE_DIR/bin
MODULES_DIR=$NEXUSCORE_DIR/modules
OVERLAY_DIR=$NEXUSCORE_DIR/overlay
LOGS_DIR=$NEXUSCORE_DIR/logs
TMP_DIR=$NEXUSCORE_DIR/tmp

mkdir -p "$BIN_DIR" "$MODULES_DIR" "$OVERLAY_DIR" "$LOGS_DIR" "$TMP_DIR"

# ===== 放置二进制 =====
ui_print "- 安装 nexusd 二进制"
cp -f "$MODPATH/system/bin/nexusd" "$BIN_DIR/nexusd"
chmod 755 "$BIN_DIR/nexusd"
cp -f "$MODPATH/system/bin/nexuscli" "$BIN_DIR/nexuscli"
chmod 755 "$BIN_DIR/nexuscli"

# ===== init.rc 服务定义 =====
# 通过 Magisk 的 service.d 钩子启动，避免直接修改 init.rc
mkdir -p /data/adb/service.d
cat > /data/adb/service.d/nexusd.sh <<'EOF'
#!/system/bin/sh
# NexusCore Daemon 启动脚本（Magisk service.d 钩子）
NEXUSD=/data/adb/nexuscore/bin/nexusd
if [ -x "$NEXUSD" ]; then
    # 安全模式检查
    if [ -f /data/adb/nexuscore/safe_mode ]; then
        "$NEXUSD" --readonly &
    else
        "$NEXUSD" &
    fi
fi
EOF
chmod 755 /data/adb/service.d/nexusd.sh

# ===== 解析 Manager UID =====
# 整改 #5：daemon 启动时需要 Manager UID 给 socket chown
# 在这里（Magisk post-fs-data 阶段）解析，避免 daemon 启动时反复 fork pm
ui_print "- 解析 NexusManager UID"
MANAGER_PKG=com.nexus.manager
MANAGER_UID=$(pm path $MANAGER_PKG 2>/dev/null | head -1 | xargs stat -c %u 2>/dev/null)
if [ -z "$MANAGER_UID" ]; then
    # 试 debug 包名
    MANAGER_PKG=com.nexus.manager.debug
    MANAGER_UID=$(pm path $MANAGER_PKG 2>/dev/null | head -1 | xargs stat -c %u 2>/dev/null)
fi

if [ -n "$MANAGER_UID" ]; then
    echo "$MANAGER_UID" > /data/adb/nexuscore/manager_uid
    chmod 644 /data/adb/nexuscore/manager_uid
    ui_print "- Manager UID: $MANAGER_UID ($MANAGER_PKG)"
else
    rm -f /data/adb/nexuscore/manager_uid
    ui_print "! NexusManager 未安装，daemon 将退化为 0666 + SO_PEERCRED 模式"
fi

# ===== 清理卸载标记 =====
rm -f /data/adb/nexuscore/.uninstall_pending

ui_print "- 安装完成，重启后生效"
ui_print "- Daemon 路径: $BIN_DIR/nexusd"
ui_print "- 日志路径: $LOGS_DIR/nexusd.log"

set_perm_recursive "$NEXUSCORE_DIR" 0 0 0755 0644
set_perm "$BIN_DIR/nexusd" 0 0 0755
set_perm "$BIN_DIR/nexuscli" 0 0 0755
