#pragma once

#include "nexus/types.h"
#include "nexus/util.h"
#include <memory>

namespace nexus {

// 探测当前 Root 环境与内核能力
class RootEnvironmentDetector {
public:
    // 综合检测：探测 provider / sepolicy / overlayfs / 动态分区 / 内核能力
    // 失败原因：所有 provider 都未找到（返回 Err::NotFound），daemon 应进入只读模式。
    static Result<RootEnvironment> detect();

    // 仅探测 provider，不失败
    static RootProvider detectProvider();
};

} // namespace nexus
