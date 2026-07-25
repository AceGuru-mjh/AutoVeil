#!/system/bin/sh
# NexusProp Editor 安装脚本
# 读取真实 /system/build.prop，生成修改版放入模块 system/ 目录

SKIPUNZIP=0

ui_print() { echo "$1"; }

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

ui_print "- 修改完成"
ui_print "- 文件大小: $(wc -c < "$PROP_FILE") bytes"

{
    echo "$(date) installed by NexusCore"
    echo "  source: $SRC_PROP"
    echo "  ro.debuggable=$SET_DEBUGGABLE"
    echo "  ro.secure=$SET_SECURE"
} > "$MODPATH/install.log"

ui_print "- 重启后生效"

set_perm() { chmod "$3" "$1"; }
set_perm_recursive() {
    dir="$1"; own="$2"; grp="$3"; dirperm="$4"; fileperm="$5"
    find "$dir" -type d -exec chmod "$dirperm" {} \; 2>/dev/null
    find "$dir" -type f -exec chmod "$fileperm" {} \; 2>/dev/null
}
set_perm_recursive "$MODPATH/system" 0 0 0755 0644

exit 0
