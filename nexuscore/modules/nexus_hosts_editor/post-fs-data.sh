#!/system/bin/sh
# NexusHosts Editor post-fs-data 阶段
# 此时 /system/etc/hosts 已被 daemon bind mount 为模块版本

echo "$(date) post-fs-data: hosts overlay active" >> "$NEXUS_MODULE_PATH/runtime.log"

exit 0
