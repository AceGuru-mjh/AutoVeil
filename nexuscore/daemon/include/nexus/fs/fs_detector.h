#pragma once

#include "nexus/fs/i_file_system_interceptor.h"
#include <memory>

namespace nexus {

// 选择器：优先 OverlayFS，失败回退 BindMount，再失败 Noop
class FsDetector {
public:
    static std::unique_ptr<IFileSystemInterceptor> select(const RootEnvironment& env);
};

} // namespace nexus
