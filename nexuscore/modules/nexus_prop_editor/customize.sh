#!/system/bin/sh
# NexusProp Editor — customize 阶段
# 读取真实 /system/build.prop，生成修改版放入模块 system/ 目录
#
# 变量说明（NexusCore 自有体系）：
#   NEXUS_MODULE_PATH  — 模块安装目录绝对路径
#   NEXUS_BOOT_STAGE   — 当前启动阶段（customize / post-fs-data / service）
#   NEXUS_ROOT_PROVIDER — Root 来源（magisk / kernelsu / apatch）
#   NEXUS_API_LEVEL    — 设备 Android API 等级

set -e

nexus_log() { echo "  $1"; }
nexus_abort() { echo "! $1"; exit 1; }
nexus_set_perm() { chmod "$3" "$1"; }
nexus_set_perm_recursive() {
  _dir="$1"; _own="$2"; _grp="$3"; _dperm="$4"; _fperm="$5"
  find "$_dir" -type d -exec chmod "$_dperm" {} \; 2>/dev/null
  find "$_dir" -type f -exec chmod "$_fperm" {} \; 2>/dev/null
}

nexus_log "NexusProp Editor 安装开始"
nexus_log "模块路径: $NEXUS_MODULE_PATH"

PROP_FILE="$NEXUS_MODULE_PATH/system/build.prop"
mkdir -p "$NEXUS_MODULE_PATH/system"

# ====== 用户配置（可在此处编辑） ======
SET_DEBUGGABLE=0      # 1=可调试，0=不可调试（推荐 0 保持安全）
SET_SECURE=1          # 1=安全，0=不安全（推荐 1）
# ====================================

if [ ! -f "/system/build.prop" ]; then
    nexus_log "/system/build.prop 不存在，可能为动态分区设备"
    nexus_log "尝试从 /system_root/system/build.prop 读取"
    SRC_PROP="/system_root/system/build.prop"
else
    SRC_PROP="/system/build.prop"
fi

if [ ! -f "$SRC_PROP" ]; then
    nexus_abort "无法定位 build.prop，安装中止"
fi

nexus_log "源文件: $SRC_PROP"
nexus_log "目标值: ro.debuggable=$SET_DEBUGGABLE, ro.secure=$SET_SECURE"

cp "$SRC_PROP" "$PROP_FILE"
chmod 644 "$PROP_FILE"

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

nexus_log "修改完成"
nexus_log "文件大小: $(wc -c < "$PROP_FILE") bytes"

{
    echo "$(date) installed by NexusCore"
    echo "  source: $SRC_PROP"
    echo "  ro.debuggable=$SET_DEBUGGABLE"
    echo "  ro.secure=$SET_SECURE"
} > "$NEXUS_MODULE_PATH/install.log"

nexus_log "重启后生效"

nexus_set_perm_recursive "$NEXUS_MODULE_PATH/system" 0 0 0755 0644

exit 0
