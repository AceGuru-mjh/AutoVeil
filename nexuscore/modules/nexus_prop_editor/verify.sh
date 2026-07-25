#!/system/bin/sh
# NexusProp Editor — verify 阶段
# 安装后/重启前自检，失败则阻止模块加载

if [ ! -f "$NEXUS_MODULE_PATH/system/build.prop" ]; then
    echo "verify failed: build.prop not found"
    exit 1
fi

if ! grep -q "^ro.debuggable=" "$NEXUS_MODULE_PATH/system/build.prop"; then
    echo "verify failed: ro.debuggable missing"
    exit 1
fi

if ! grep -q "^ro.secure=" "$NEXUS_MODULE_PATH/system/build.prop"; then
    echo "verify failed: ro.secure missing"
    exit 1
fi

echo "verify passed"
exit 0
