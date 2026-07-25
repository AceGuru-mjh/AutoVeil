#pragma once

#include "nexus/fs/i_file_system_interceptor.h"

namespace nexus {

// Bind Mount 降级实现，见 spec-01 §6.3
//
// 整改（原 bug）：原用 ::link() 做 stock 备份，跨文件系统会 EXDEV 失败。
// 改用 copyFile。
class BindMountInterceptor : public IFileSystemInterceptor {
public:
    Result<bool> detect(const RootEnvironment& env) override;
    Result<void> mountOverlay(const MountTarget& t) override;
    Result<void> unmountAll() override;
    std::string_view implName() const override { return "bind"; }
private:
    RootEnvironment env_;
};

} // namespace nexus
