#include "nexus/fs/i_file_system_interceptor.h"

namespace nexus {

Result<void> IFileSystemInterceptor::mountAll(const std::vector<MountTarget>& targets) {
    for (const auto& t : targets) {
        if (auto r = mountOverlay(t); !r) {
            return r;
        }
    }
    return {};
}

} // namespace nexus
