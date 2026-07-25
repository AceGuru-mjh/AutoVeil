#!/system/bin/sh
# NexusHosts Editor late_start 阶段
# 验证 hosts 是否生效，并记录日志

LOG="$NEXUS_MODULE_PATH/runtime.log"

{
    echo "===== $(date) service.sh ====="
    echo "当前 hosts 文件大小: $(wc -c < /system/etc/hosts 2>/dev/null) bytes"
    echo "hosts 文件 hash: $(md5sum /system/etc/hosts 2>/dev/null | awk '{print $1}')"
    echo "测试解析 ad.example.com:"
    getent hosts ad.example.com 2>/dev/null || echo "  (getent 不可用)"
    echo "================================"
} >> "$LOG"

exit 0
