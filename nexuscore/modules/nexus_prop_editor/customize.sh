#!/system/bin/sh
# NexusProp Editor 安装脚本
# 读取真实 /system/build.prop，生成修改版放入模块 system/ 目录
#
# 整改 #6（原 bug）：原脚本调用 abort() 但未定义，会打印 "abort: command not found"
# 然后继续执行 cp 一个不存在的源文件、sed 一个空文件，最后 exit 0 把一个空/损坏的
# build.prop 写入模块。
#
# 修复：在本地 shim 中定义 abort（daemon 注入的 shim 也会定义，覆盖此处的本地定义）。
#
# 注意：本脚本在 NexusCore daemon 调用时，daemon 会预先 source 一个 shim 文件
# 注入 ui_print/set_perm/set_perm_recursive/abort 等函数与 MODPATH/TMPDIR 等变量。
# 本地的 shim 定义仅为手动测试时使用，daemon 调用时会覆盖。

SKIPUNZIP=0

# ============ 本地 shim（手动测试用，daemon 调用时被覆盖） ============
ui_print() { echo "$1"; }
set_perm() { chown "$2:$3" "$1" 2>/dev/null; chmod "$4" "$1" 2>/dev/null; }
set_perm_recursive() {
    dir="$1"; own="$2"; grp="$3"; dirperm="$4"; fileperm="$5"
    chown -R "$own:$grp" "$dir" 2>/dev/null
    find "$dir" -type d -exec chmod "$dirperm" {} \; 2>/dev/null
    find "$dir" -type f -exec chmod "$fileperm" {} \; 2>/dev/null
}
# 整改 #6：定义 abort，输出错误并 exit 1
abort() { echo "! abort: $1" >&2; exit 1; }

# ============ 安装逻辑 ============
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
ui_print "- 注意：Android 12+ 上 build.prop 可能被分散到多个文件，"
ui_print "-       bind mount 单个 build.prop 不一定改变 getprop 输出。"
ui_print "-       若需稳定生效，请改用 nexus_hosts_editor 示例模块。"

set_perm_recursive "$MODPATH/system" 0 0 0755 0644

exit 0
