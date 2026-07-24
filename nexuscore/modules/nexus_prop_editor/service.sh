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

if [ -x "/data/adb/nexuscore/bin/nexuscli" ]; then
    /data/adb/nexuscore/bin/nexuscli emit EVENT_MODULE_REPORT \
        --module="$NEXUS_MODULE_ID" \
        --data="debuggable=$(getprop ro.debuggable),secure=$(getprop ro.secure)" \
        2>/dev/null || true
fi

exit 0
