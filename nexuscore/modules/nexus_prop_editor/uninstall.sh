#!/system/bin/sh
# NexusProp Editor — uninstall 阶段
# 模块被卸载时清理运行时数据

rm -f "$NEXUS_MODULE_PATH/runtime.log"
rm -f "$NEXUS_MODULE_PATH/install.log"

exit 0
