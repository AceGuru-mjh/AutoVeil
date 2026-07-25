#include "nexus/fs/fs_detector.h"
#include "nexus/fs/overlayfs_interceptor.h"
#include "nexus/fs/bind_mount_interceptor.h"
#include "nexus/fs/noop_interceptor.h"
#include "nexus/log.h"
#include <sys/stat.h>

namespace nexus {

std::unique_ptr<IFileSystemInterceptor> FsDetector::select(const RootEnvironment& env) {
    // Phase 1.7 修复：原代码优先 OverlayFS，但 OverlayFsInterceptor 对单文件目标
    // 会返回 Unsupported。为避免每次 mountOverlay 都试错，FsDetector 应直接根据
    // 使用场景选择：
    // - NexusCore MVP 主要场景是单文件替换（system/etc/hosts, system/build.prop 等）
    //   → 优先 BindMount（OverlayFS 不适合单文件）
    // - 未来若支持整目录覆盖（如 system/etc/ 整目录），再用 OverlayFS
    //
    // 因此改为：优先 BindMount，OverlayFS 作为 Phase 2+ 整目录覆盖的选项

    auto bm = std::make_unique<BindMountInterceptor>();
    if (auto ok = bm->detect(env); ok && *ok) {
        NX_LOG_I("FsDetector", "using BindMountInterceptor (MVP default for single-file mounts)");
        return bm;
    }

    // BindMount detect 始终返回 true，理论不会到这
    auto ov = std::make_unique<OverlayFsInterceptor>();
    if (auto ok = ov->detect(env); ok && *ok) {
        NX_LOG_I("FsDetector", "using OverlayFsInterceptor (for directory-level overlay)");
        return ov;
    }

    NX_LOG_E("FsDetector", "no FS interceptor available; running read-only");
    return std::make_unique<NoopInterceptor>();
}

} // namespace nexus
