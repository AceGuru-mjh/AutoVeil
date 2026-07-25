#!/system/bin/sh
# NexusProp Editor 卸载脚本
# Daemon 会自动 umount system/build.prop，本脚本只清理运行时数据

LOG="/data/adb/nexuscore/logs/$NEXUS_MODULE_ID.uninstall.log"
{
    echo "$(date) uninstalling $NEXUS_MODULE_ID"
    echo "runtime.log tail:"
    tail -n 20 "$NEXUS_MODULE_PATH/runtime.log" 2>/dev/null
} > "$LOG"

exit 0
