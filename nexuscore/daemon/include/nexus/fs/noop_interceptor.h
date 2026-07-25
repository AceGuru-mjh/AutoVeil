#pragma once

#include "nexus/fs/i_file_system_interceptor.h"

namespace nexus {

// No-op 实现，用于 read-only 模式（FS 探测全失败时）
// 所有 mount 直接返回成功但什么都不做，仅记录 warning。
class NoopInterceptor : public IFileSystemInterceptor {
public:
    Result<bool> detect(const RootEnvironment& env) override { return true; }
    Result<void> mountOverlay(const MountTarget& t) override;
    Result<void> unmountAll() override { return {}; }
    std::string_view implName() const override { return "noop"; }
};

} // namespace nexus
