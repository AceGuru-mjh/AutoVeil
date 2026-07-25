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
# 整改 #15（原 bug）：原写法 [ "$API" -lt 30 ] 2>/dev/null
#   当 API 为空（getprop 失败）时，[ "" -lt 30 ] 会报错退出码 2，
#   2>/dev/null 抑制 stderr，if 条件因非零退出码而视为 false，
#   继续安装而非拒绝，违反 verify 的初衷。
#   修复：显式检查 API 非空 + 数值比较。
if [ -z "$API" ]; then
    echo "! 无法读取 Android API level（getprop 失败）" >&2
    exit 1
fi
if [ "$API" -lt 30 ]; then
    echo "! Android API $API 过低，需要 >= 30" >&2
    exit 1
fi

# 整改：警告 Android 12+ 上 build.prop 可能不可靠
if [ "$API" -ge 31 ]; then
    echo "w: Android API $API >= 31，build.prop 可能被分散到多个文件，" >&2
    echo "w: bind mount 单个 build.prop 不一定改变 getprop 输出。" >&2
    echo "w: 此模块仅作为 capabilities 演示，可能不实际生效。" >&2
fi

exit 0
