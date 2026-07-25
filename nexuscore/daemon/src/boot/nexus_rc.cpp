// NexusCore init.rc service 定义
//
// Phase 4：定义 NexusCore daemon 在 init 早期启动的 service
//
// 这个文件会被注入到 boot image 的 ramdisk 中（详见 boot_patcher.cpp）
// init 在解析 ramdisk 中的 .rc 文件时会自动启动 nexusd service
//
// 启动时机：
//   - on early-init: 创建必要目录
//   - on init: 设置 SELinux 域
//   - on post-fs-data: 启动 nexusd daemon（root 已就绪）
//   - on boot: 启动 SuDaemon
//
// 注意：本文件是 Android init.rc 语法，不是 shell 脚本

// ============ NexusCore init.rc ============
// 这个 service 会在 post-fs-data 阶段启动 nexusd daemon

#include <string>

namespace nexus {

/// 生成 nexus.rc service 定义内容
std::string generateNexusRc() {
    return R"init_rc(# NexusCore init.rc
# 自动生成，请勿手动修改

# ====== early-init: 创建目录 ======
on early-init
    mkdir /data/adb/nexuscore 0755 root root
    mkdir /data/adb/nexuscore/bin 0755 root root
    mkdir /data/adb/nexuscore/modules 0755 root root
    mkdir /data/adb/nexuscore/overlay 0755 root root
    mkdir /data/adb/nexuscore/logs 0755 root root
    mkdir /data/adb/nexuscore/tmp 0700 root root
    mkdir /data/adb/nexuscore/hook_modules 0755 root root
    mkdir /data/adb/nexuscore/hook_sockets 0755 root root
    mkdir /data/adb/nexuscore/backup 0700 root root

# ====== init: 设置 SELinux 域 ======
on init
    # 等待 SELinux 加载完成
    wait /sys/fs/selinux/enforce

# ====== post-fs-data: 启动 nexusd ======
on post-fs-data
    # 检查是否需要恢复原始 boot（卸载场景）
    exec - root -- /system/bin/sh -c "if [ -f /data/adb/nexuscore/.uninstall_pending ]; then \
        /data/adb/nexuscore/bin/nexusd --restore-boot; \
        rm /data/adb/nexuscore/.uninstall_pending; \
    fi"

    # 启动 nexusd daemon
    start nexusd

# ====== boot: 启动 SuDaemon ======
on property:sys.boot_completed=1
    start nexus_su

# ====== service 定义 ======

# nexusd: NexusCore 主守护进程
# 提供：模块加载、文件系统挂载、IPC server、SELinux 策略注入
service nexusd /data/adb/nexuscore/bin/nexusd
    class core
    user root
    group root system
    seclabel u:r:nexus_daemon:s0
    oneshot
    disabled
    # 不重启（oneshot），由 post-fs-data 触发

# nexus_su: SU 授权守护进程
# 提供：su 客户端连接、root shell fork、策略管理
service nexus_su /data/adb/nexuscore/bin/nexusd --su-daemon
    class main
    user root
    group root system
    seclabel u:r:nexus_daemon:s0
    disabled
    # 在 boot_completed 后启动

# nexushook: Zygote 注入器（可选）
# 提供：zygote fork 监听、模块 .so 注入
service nexushook /data/adb/nexuscore/bin/nexushook
    class main
    user root
    group root system
    seclabel u:r:nexus_daemon:s0
    disabled
)init_rc";
}

/// 生成 nexusinit bootstrap 脚本
///
/// 这个脚本在 init 早期由 nexus.rc 调用，确保 nexusd 二进制存在
std::string generateBootstrapScript() {
    return R"bootstrap(#!/system/bin/sh
# NexusCore bootstrap script
# 由 init 在 post-fs-data 阶段调用

set -e

NEXUSCORE_DIR=/data/adb/nexuscore
NEXUSD=$NEXUSCORE_DIR/bin/nexusd

# 创建必要目录
mkdir -p $NEXUSCORE_DIR/bin
mkdir -p $NEXUSCORE_DIR/modules
mkdir -p $NEXUSCORE_DIR/overlay
mkdir -p $NEXUSCORE_DIR/logs
mkdir -p $NEXUSCORE_DIR/tmp
mkdir -p $NEXUSCORE_DIR/hook_modules
mkdir -p $NEXUSCORE_DIR/hook_sockets
mkdir -p $NEXUSCORE_DIR/backup

# 写入版本标记
echo "1.0.0" > $NEXUSCORE_DIR/.version

# 标记 bootstrap 完成
touch $NEXUSCORE_DIR/.bootstrapped

# 启动 daemon（如果未运行）
if [ -x "$NEXUSD" ]; then
    if ! pgrep -x nexusd > /dev/null 2>&1; then
        $NEXUSD &
    fi
fi

exit 0
)bootstrap";
}

} // namespace nexus
