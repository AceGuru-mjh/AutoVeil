#!/system/bin/sh
# 校验当前环境是否支持本模块

if [ -z "$NEXUS_VERSION" ]; then
    echo "! 不在 NexusCore 环境中" >&2
    exit 1
fi

if [ ! -r "/system/build.prop" ] && [ ! -r "/system_root/system/build.prop" ]; then
    echo "! 找不到可读的 build.prop" >&2
    exit 1
fi

API=$(getprop ro.build.version.sdk)
if [ "$API" -lt 30 ] 2>/dev/null; then
    echo "! Android API $API 过低，需要 >= 30" >&2
    exit 1
fi

exit 0
