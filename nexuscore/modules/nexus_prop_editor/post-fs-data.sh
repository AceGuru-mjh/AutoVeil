#!/system/bin/sh
# NexusProp Editor — post-fs-data 阶段
# 此阶段 zygote 未启动，build.prop 已由 Daemon bind mount

echo "$(date) post-fs-data: build.prop overlay active" >> "$NEXUS_MODULE_PATH/runtime.log"

exit 0
