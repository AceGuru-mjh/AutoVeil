#include "nexus/fs/overlayfs_interceptor.h"
#include "nexus/util.h"
#include "nexus/log.h"

#include <linux/loop.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <unistd.h>

namespace nexus {

Result<bool> OverlayFsInterceptor::detect(const RootEnvironment& env) {
    if (!env.overlayFsAvailable) return false;
    // 探针：在 tmp 上挂一个 overlay，成功即支持
    std::string probeLower1 = "/data/adb/nexuscore/.probe/lower1";
    std::string probeLower2 = "/data/adb/nexuscore/.probe/lower2";
    std::string probeUpper  = "/data/adb/nexuscore/.probe/upper";
    std::string probeWork   = "/data/adb/nexuscore/.probe/work";
    std::string probeMnt    = "/data/adb/nexuscore/.probe/mnt";
    mkdirRecursive(probeLower1, 0755);
    mkdirRecursive(probeLower2, 0755);
    mkdirRecursive(probeUpper, 0755);
    mkdirRecursive(probeWork, 0755);
    mkdirRecursive(probeMnt, 0755);

    std::string opts = "lowerdir=" + probeLower2 + ":" + probeLower1
                     + ",upperdir=" + probeUpper
                     + ",workdir="  + probeWork;
    int r = ::mount("overlay", probeMnt.c_str(), "overlay",
                    MS_NODEV | MS_NOATIME, opts.c_str());
    if (r < 0) {
        NX_LOG_D("OverlayFs", "probe failed: %s", ::strerror(errno));
        return false;
    }
    ::umount2(probeMnt.c_str(), MNT_DETACH);
    // 清理（不严格，残留不影响功能）
    env_ = env;
    return true;
}

Result<void> OverlayFsInterceptor::mountOverlay(const MountTarget& t) {
    // 整改 #2（原 bug）：原写法 lowerdir=<file>:<file> 在 mount(2) 时 EINVAL。
    // 正确做法：构造目录树镜像，mount target 是父目录（目录而非文件）。
    auto slash = t.target.find_last_of('/');
    if (slash == std::string::npos) return std::unexpected(Err::InvalidArg);
    std::string parentDir = t.target.substr(0, slash);
    std::string baseName  = t.target.substr(slash + 1);

    std::string hashStr   = hashPath(t.target);
    std::string stockRoot = env_.overlayBase + "/stock/" + hashStr;
    std::string modRoot   = env_.overlayBase + "/mod/"   + hashStr;
    std::string upper     = env_.overlayBase + "/upper/" + hashStr;
    std::string work      = env_.overlayBase + "/work/"  + hashStr;

    std::string stockFile = stockRoot + parentDir + "/" + baseName;
    if (!probeFile(stockFile)) {
        if (!mkdirRecursive(stockRoot + parentDir)) return std::unexpected(Err::IoError);
        // 整改 #3：用 copy 而非 link（跨 fs 不会 EXDEV）
        if (!copyFile(t.target, stockFile)) {
            NX_LOG_W("OverlayFs", "stock copy failed for %s; will try anyway", t.target.c_str());
        }
    }
    std::string modFile = modRoot + parentDir + "/" + baseName;
    if (!mkdirRecursive(modRoot + parentDir)) return std::unexpected(Err::IoError);
    if (!copyFile(t.source, modFile)) return std::unexpected(Err::IoError);
    mkdirRecursive(upper, 0755);
    mkdirRecursive(work, 0755);

    std::string opts = "lowerdir=" + modRoot + ":" + stockRoot
                     + ",upperdir=" + upper
                     + ",workdir="  + work;
    if (::mount("overlay", parentDir.c_str(), "overlay",
                MS_NODEV | MS_NOATIME, opts.c_str()) < 0) {
        NX_LOG_W("OverlayFs", "mount failed for %s: %s",
                 parentDir.c_str(), ::strerror(errno));
        return std::unexpected(Err::MountFailed);
    }
    mounted_.push_back(parentDir);
    NX_LOG_I("OverlayFs", "mounted overlay on %s (module=%s)",
             parentDir.c_str(), t.moduleId.c_str());
    return {};
}

Result<void> OverlayFsInterceptor::unmountAll() {
    for (auto it = mounted_.rbegin(); it != mounted_.rend(); ++it) {
        ::umount2(it->c_str(), MNT_DETACH);
    }
    mounted_.clear();
    return {};
}

} // namespace nexus
