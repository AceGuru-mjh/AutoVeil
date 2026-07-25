#include "nexus/fs/fs_detector.h"
#include "nexus/fs/overlayfs_interceptor.h"
#include "nexus/fs/bind_mount_interceptor.h"
#include "nexus/fs/noop_interceptor.h"
#include "nexus/log.h"

namespace nexus {

std::unique_ptr<IFileSystemInterceptor> FsDetector::select(const RootEnvironment& env) {
    auto ov = std::make_unique<OverlayFsInterceptor>();
    if (auto ok = ov->detect(env); ok && *ok) {
        NX_LOG_I("FsDetector", "using OverlayFsInterceptor");
        return ov;
    }
    NX_LOG_W("FsDetector", "OverlayFS unavailable, fallback to BindMount");

    auto bm = std::make_unique<BindMountInterceptor>();
    if (auto ok = bm->detect(env); ok && *ok) {
        NX_LOG_I("FsDetector", "using BindMountInterceptor");
        return bm;
    }

    NX_LOG_E("FsDetector", "no FS interceptor available; running read-only");
    return std::make_unique<NoopInterceptor>();
}

} // namespace nexus
