#pragma once

#include <string>
#include <map>

namespace nexus {

/// 设备能力枚举
///
/// 不同设备支持的能力不同，CapabilityManager 在 daemon 启动时
/// 检测当前设备支持哪些能力，生成能力矩阵。
/// 模块和功能根据能力矩阵决定是否启用。
enum class Capability {
    ROOT_ACCESS,          // 当前进程是否有 root 权限 (uid=0)
    BOOT_PATCH,           // 是否支持 boot image 修补（实验性，默认关闭）
    SELINUX_CONTROL,      // 是否能控制 SELinux 策略
    MOUNT_NAMESPACE,      // 是否支持 mount namespace 隔离
    ZYGOTE_HOOK,          // 是否支持 zygote 注入（需 nexushook）
    IPC_CONTROL,          // 是否有 IPC server 权限
    OVERLAY_FS,           // 内核是否支持 overlayfs
    DYNAMIC_PARTITIONS,   // 是否为动态分区设备
};

/// 能力管理器（单例）
///
/// 启动流程：
///   1. daemon 启动时调用 CapabilityManager::instance().detect()
///   2. 检测所有能力，生成能力矩阵
///   3. 后续功能模块根据 has(Capability::XXX) 决定是否启用
///
/// 这解决了"不同设备能力不同"的核心问题：
/// - Pixel 设备可能支持 BOOT_PATCH + OVERLAY_FS
/// - Samsung 设备可能 BOOT_PATCH=false（Knox 保护）
/// - 模拟器可能 SELINUX_CONTROL=false
class CapabilityManager {
public:
    static CapabilityManager& instance();

    /// 检测当前设备的所有能力
    void detect();

    /// 查询指定能力是否可用
    bool has(Capability capability);

    /// 生成人类可读的能力报告
    std::string report();

private:
    CapabilityManager() = default;

    /// 检测 root 权限
    void detectRoot();

    /// 检测 SELinux 控制
    void detectSelinux();

    /// 检测 mount namespace
    void detectMountNamespace();

    /// 检测 overlayfs
    void detectOverlayFs();

    /// 检测动态分区
    void detectDynamicPartitions();

    /// 检测 zygote hook（nexushook 是否可用）
    void detectZygoteHook();

    /// 检测 IPC 权限
    void detectIpcControl();

private:
    std::map<Capability, bool> capabilities;
};

} // namespace nexus
