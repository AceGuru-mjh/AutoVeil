#include "nexus/fs/noop_interceptor.h"
#include "nexus/log.h"

namespace nexus {

Result<void> NoopInterceptor::mountOverlay(const MountTarget& t) {
    NX_LOG_W("NoopFs", "skip mount (read-only mode): %s -> %s (module=%s)",
             t.source.c_str(), t.target.c_str(), t.moduleId.c_str());
    return {};
}

} // namespace nexus
