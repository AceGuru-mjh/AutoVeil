#include "nexus/permission/provider.h"
#include "nexus/util.h"
#include "nexus/log.h"
#include <unistd.h>

namespace nexus {

PermissionLevel NoRootProvider::level() {
    if (::getuid() == 0) {
        return PermissionLevel::ROOT;
    }
    return PermissionLevel::NONE;
}

bool NoRootProvider::execute(const std::string& command) {
    // 用户态阶段禁止越权执行
    // 未来 NexusRootProvider 会实现真正的 root 执行
    NX_LOG_W("Permission", "NoRootProvider: refusing to execute: %s",
             command.c_str());
    return false;
}

std::string NoRootProvider::name() {
    return "none";
}

} // namespace nexus
