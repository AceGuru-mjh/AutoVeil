#include "nexus/capability/capability_manager.h"
#include "nexus/util.h"
#include "nexus/log.h"

#include <unistd.h>
#include <sys/stat.h>

namespace nexus {

CapabilityManager&
CapabilityManager::instance() {
    static CapabilityManager manager;
    return manager;
}

void CapabilityManager::detect() {
    detectRoot();
    detectSelinux();
    detectMountNamespace();
    detectOverlayFs();
    detectDynamicPartitions();
    detectZygoteHook();
    detectIpcControl();

    NX_LOG_I("Capability", "=== Capability Report ===");
    NX_LOG_I("Capability", "ROOT_ACCESS:         %s", has(Capability::ROOT_ACCESS) ? "YES" : "NO");
    NX_LOG_I("Capability", "BOOT_PATCH:          %s", has(Capability::BOOT_PATCH) ? "YES" : "NO");
    NX_LOG_I("Capability", "SELINUX_CONTROL:     %s", has(Capability::SELINUX_CONTROL) ? "YES" : "NO");
    NX_LOG_I("Capability", "MOUNT_NAMESPACE:     %s", has(Capability::MOUNT_NAMESPACE) ? "YES" : "NO");
    NX_LOG_I("Capability", "ZYGOTE_HOOK:         %s", has(Capability::ZYGOTE_HOOK) ? "YES" : "NO");
    NX_LOG_I("Capability", "IPC_CONTROL:         %s", has(Capability::IPC_CONTROL) ? "YES" : "NO");
    NX_LOG_I("Capability", "OVERLAY_FS:          %s", has(Capability::OVERLAY_FS) ? "YES" : "NO");
    NX_LOG_I("Capability", "DYNAMIC_PARTITIONS:  %s", has(Capability::DYNAMIC_PARTITIONS) ? "YES" : "NO");
}

void CapabilityManager::detectRoot() {
    capabilities[Capability::ROOT_ACCESS] = (::getuid() == 0);
}

void CapabilityManager::detectSelinux() {
    // 检查 /sys/fs/selinux/enforce 是否可读
    // 以及 /sys/fs/selinux/load 是否可写
    bool canRead = probeFile("/sys/fs/selinux/enforce");
    bool canWrite = (::access("/sys/fs/selinux/load", W_OK) == 0);
    capabilities[Capability::SELINUX_CONTROL] = canRead && canWrite;
}

void CapabilityManager::detectMountNamespace() {
#ifdef __linux__
    // Linux 内核支持 CLONE_NEWNS
    // 检查 /proc/self/ns 是否存在（所有 Linux 都有）
    capabilities[Capability::MOUNT_NAMESPACE] = probeDir("/proc/self/ns");
#else
    capabilities[Capability::MOUNT_NAMESPACE] = false;
#endif
}

void CapabilityManager::detectOverlayFs() {
    capabilities[Capability::OVERLAY_FS] = kernelSupports("overlay");
}

void CapabilityManager::detectDynamicPartitions() {
    capabilities[Capability::DYNAMIC_PARTITIONS] = isDynamicPartitions();
}

void CapabilityManager::detectZygoteHook() {
    // 检查 nexushook 二进制是否存在
    capabilities[Capability::ZYGOTE_HOOK] = probeFile("/data/adb/nexuscore/bin/nexushook");
}

void CapabilityManager::detectIpcControl() {
    // 检查是否有创建 UDS socket 的权限
    // root 总是有权限；非 root 检查 /dev/socket 是否可写
    if (has(Capability::ROOT_ACCESS)) {
        capabilities[Capability::IPC_CONTROL] = true;
    } else {
        capabilities[Capability::IPC_CONTROL] =
            (::access("/dev/socket", W_OK) == 0);
    }
}

bool CapabilityManager::has(Capability capability) {
    auto it = capabilities.find(capability);
    if (it == capabilities.end()) return false;
    return it->second;
}

std::string CapabilityManager::report() {
    std::string result;
    result += "=== AutoVeil Capability Report ===\n";
    result += "Root Access:        " + std::string(has(Capability::ROOT_ACCESS) ? "YES" : "NO") + "\n";
    result += "Boot Patch:         " + std::string(has(Capability::BOOT_PATCH) ? "YES" : "NO") + "\n";
    result += "SELinux Control:    " + std::string(has(Capability::SELINUX_CONTROL) ? "YES" : "NO") + "\n";
    result += "Mount Namespace:    " + std::string(has(Capability::MOUNT_NAMESPACE) ? "YES" : "NO") + "\n";
    result += "Zygote Hook:        " + std::string(has(Capability::ZYGOTE_HOOK) ? "YES" : "NO") + "\n";
    result += "IPC Control:        " + std::string(has(Capability::IPC_CONTROL) ? "YES" : "NO") + "\n";
    result += "OverlayFS:          " + std::string(has(Capability::OVERLAY_FS) ? "YES" : "NO") + "\n";
    result += "Dynamic Partitions: " + std::string(has(Capability::DYNAMIC_PARTITIONS) ? "YES" : "NO") + "\n";
    return result;
}

} // namespace nexus
