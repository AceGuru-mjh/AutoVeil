#include "nexus/fs/bind_mount_interceptor.h"
#include "nexus/util.h"
#include "nexus/log.h"

#include <sys/mount.h>
#include <sys/stat.h>
#include <unistd.h>

namespace nexus {

Result<bool> BindMountInterceptor::detect(const RootEnvironment& env) {
    env_ = env;
    return true;   // bind mount 几乎所有内核都支持
}

Result<void> BindMountInterceptor::mountOverlay(const MountTarget& t) {
    // 整改 #3（原 bug）：原用 ::link 做 stock 备份，跨 fs 会 EXDEV。
    std::string stock = env_.overlayBase + "/stock/" + hashPath(t.target);
    if (!probeFile(stock)) {
        mkdirRecursive(env_.overlayBase + "/stock", 0755);
        if (!copyFile(t.target, stock)) {
            NX_LOG_W("BindMount", "backup stock failed for %s (errno=%d); continue",
                     t.target.c_str(), errno);
            // 备份失败不阻断 bind mount，umount 后 target 自然恢复
        }
    }
    // 整改：原写法 MS_BIND | MS_REC 在单文件 bind 上会 EINVAL，去掉 MS_REC
    if (::mount(t.source.c_str(), t.target.c_str(), nullptr,
                MS_BIND, nullptr) < 0) {
        NX_LOG_W("BindMount", "bind failed for %s: %s",
                 t.target.c_str(), ::strerror(errno));
        return std::unexpected(Err::MountFailed);
    }
    // 只读重挂
    ::mount(t.source.c_str(), t.target.c_str(), nullptr,
            MS_BIND | MS_REMOUNT | MS_RDONLY, nullptr);
    mounted_.push_back(t.target);
    NX_LOG_I("BindMount", "bind-mounted %s -> %s (module=%s)",
             t.source.c_str(), t.target.c_str(), t.moduleId.c_str());
    return {};
}

Result<void> BindMountInterceptor::unmountAll() {
    for (auto it = mounted_.rbegin(); it != mounted_.rend(); ++it) {
        ::umount2(it->c_str(), MNT_DETACH);
    }
    mounted_.clear();
    return {};
}

} // namespace nexus
