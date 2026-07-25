#include "nexus/selinux_manager.h"
#include "nexus/util.h"
#include "nexus/log.h"

#ifdef __ANDROID__
#include <cutils/properties.h>
#endif
#include <fcntl.h>
#include <fstream>
#include <sstream>
#include <sys/stat.h>
#include <unistd.h>

namespace nexus {

std::string SELinuxManager::currentContext() {
    auto content = readFile("/proc/self/attr/current");
    return content.value_or("");
}

bool SELinuxManager::setContext(const std::string& ctx) {
    // Phase 1.7 修复：原写 /proc/self/attr/exec 是设置下次 exec 的 context，
    // 但 daemon 不 exec，所以原代码无效。改为 /proc/self/attr/current（即时切换）。
    // 同时 SELinux attr 写入需要 null 终止。
    int fd = ::open("/proc/self/attr/current", O_WRONLY | O_CLOEXEC);
    if (fd < 0) return false;
    std::string nul_terminated = ctx + '\0';
    bool ok = ::write(fd, nul_terminated.data(), nul_terminated.size()) ==
              (ssize_t)nul_terminated.size();
    ::close(fd);
    return ok;
}

bool SELinuxManager::isEnforcing() {
    auto content = readFile("/sys/fs/selinux/enforce");
    if (!content) return true;   // 默认认为 enforcing（保守）
    return trim(*content) == "1";
}

bool SELinuxManager::execPolicyTool(const std::string& rule) {
    std::string tool;
    switch (env_.provider) {
        case RootProvider::Magisk:
            tool = "/data/adb/magisk/magiskpolicy";
            break;
        case RootProvider::KernelSU:
            // KSU 内置 hook，无需外部工具；通过 ksud 注入
            tool = "/data/adb/ksu/bin/ksud";
            break;
        case RootProvider::APatch:
            // APatch 通过 kpm 模块注入，shell out 路径需要 kpm 配合
            tool = "/data/adb/ap/apd";
            break;
        case RootProvider::NexusCore:
            // Phase 1：NexusCore 自身作为 root provider
            // 用自研的 nexuspolicy 工具（基于 libsepol 自研实现）
            tool = "/data/adb/nexuscore/bin/nexuspolicy";
            break;
        case RootProvider::None:
            return false;
    }
    if (!probeFile(tool)) {
        NX_LOG_W("SELinux", "policy tool not found: %s", tool.c_str());
        return false;
    }

    std::string cmd = tool + " --live \"" + rule + "\" 2>&1";
    auto r = execCommand(cmd, 10);
    if (r.exitCode != 0) {
        NX_LOG_W("SELinux", "policy tool failed: %s (exit=%d) out=%s err=%s",
                 rule.c_str(), r.exitCode, r.stdout_.c_str(), r.stderr_.c_str());
        return false;
    }
    return true;
}

Result<void> SELinuxManager::patchSelfDomain() {
    // 注入必要规则（详见 spec-01 §4 + §13.2）
    // 注意：每个 provider 的策略工具语法略有差异，这里用 Magisk 兼容语法
    static const char* rules[] = {
        // 自身域的 capability
        "allow nexus_daemon self:capability { sys_admin sys_boot setuid setgid setpcap }",
        // 文件系统
        "allow nexus_daemon self:filesystem { mount unmount }",
        "allow nexus_daemon tmpfs:dir { mounton add_name write }",
        "allow nexus_daemon overlay:filesystem { mount }",
        // 进程管理
        "allow nexus_daemon self:process { setcurrent setfscreate }",
        // 网络与 IPC（IPC socket）
        "allow nexus_daemon self:unix_stream_socket { create listen accept bind read write }",
        "allow nexus_daemon adb_data_file:dir { search write add_name remove_name }",
        "allow nexus_daemon adb_data_file:file { create read write open unlink getattr setattr }",
        // SELinux 策略文件（可选）
        "allow nexus_daemon selinuxfs:file { write }",
        // property_service（reboot USERSPACE 需要）
        "allow nexus_daemon init:property_service { set }",
        "allow nexus_daemon kernel:property { set }",
        // 进程转 domain
        "allow nexus_daemon self:transition { transition }",
    };

    bool anyOk = false;
    for (auto& rule : rules) {
        if (execPolicyTool(rule)) anyOk = true;
    }
    if (!anyOk) {
        return {unexpect, Err::IoError};
    }

    // 转到 nexus_daemon 域
    if (!setContext("u:r:nexus_daemon:s0")) {
        NX_LOG_W("SELinux", "setContext failed; daemon runs in default domain");
    }
    return {};
}

} // namespace nexus
