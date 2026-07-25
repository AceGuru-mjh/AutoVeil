#!/system/bin/sh
# NexusHosts Editor 环境校验

# 1. 必须是 NexusCore 1.0+
if [ -z "$NEXUS_VERSION" ]; then
    echo "! 不在 NexusCore 环境中" >&2
    exit 1
fi

# 2. Android API 必须 >= 30
API=$(getprop ro.build.version.sdk)
if [ -z "$API" ]; then
    echo "! 无法读取 Android API level" >&2
    exit 1
fi
if [ "$API" -lt 30 ]; then
    echo "! Android API $API 过低，需要 >= 30" >&2
    exit 1
fi

# 3. /system/etc/hosts 路径必须可读或不存在（不存在时 customize.sh 会创建）
# 不强制要求原始 hosts 存在，因为模块可以新建
exit 0
