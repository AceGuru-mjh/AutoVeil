#pragma once

#include "nexus/fs/i_file_system_interceptor.h"

namespace nexus {

// OverlayFS 实现，见 spec-01 §6.2
//
// 整改（原 bug）：原写法 lowerdir=<file>:<file> 错误，改为目录树镜像方式。
class OverlayFsInterceptor : public IFileSystemInterceptor {
public:
    Result<bool> detect(const RootEnvironment& env) override;
    Result<void> mountOverlay(const MountTarget& t) override;
    Result<void> unmountAll() override;
    std::string_view implName() const override { return "overlayfs"; }
private:
    RootEnvironment env_;
};

} // namespace nexus
