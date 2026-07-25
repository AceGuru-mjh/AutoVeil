#pragma once

#include "nexus/types.h"
#include "nexus/util.h"

namespace nexus {

// SELinux 策略管理
//
// MVP 简化：不直接 link libsepol，而是 shell out 调用底层 root 提供的策略工具：
//   - Magisk: /data/adb/magisk/magiskpolicy
//   - KernelSU: 内置 hook，无需外部工具
//   - APatch: kpm 模块
//
// patchSelfDomain() 向 nexus_daemon 域添加必要 allow 规则：
//   allow nexus_daemon init:property_service { set };
//   allow nexus_daemon kernel:property { set };
//   allow nexus_daemon self:capability { sys_admin sys_boot };
//   ... etc
class SELinuxManager {
public:
    explicit SELinuxManager(const RootEnvironment& env) : env_(env) {}

    // 注入自身域策略。失败不致命（restricted mode）。
    Result<void> patchSelfDomain();

    // 查询当前进程的 SELinux context
    static std::string currentContext();

    // 设置当前进程的 SELinux context（需先 patchSelfDomain）
    static bool setContext(const std::string& ctx);

    // 查询当前 enforcing 模式
    static bool isEnforcing();

private:
    const RootEnvironment& env_;

    // 通过底层 root 的策略工具注入规则
    bool execPolicyTool(const std::string& rule);
};

} // namespace nexus
