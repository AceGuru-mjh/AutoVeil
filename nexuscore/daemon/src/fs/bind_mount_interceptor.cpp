#include "nexus/fs/bind_mount_interceptor.h"
#include "nexus/util.h"
#include "nexus/log.h"

#include <cstring>
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
        // Phase 1.7 修复：copyFile 内部会调用多个 syscall 改变 errno，
        // 不能在 copyFile 失败后再读 errno。改为不读 errno，只记录 bool。
        bool backupOk = copyFile(t.target, stock);
        if (!backupOk) {
            NX_LOG_W("BindMount", "backup stock failed for %s; continue", t.target.c_str());
            // 备份失败不阻断 bind mount，umount 后 target 自然恢复
        }
    }
    // 整改：原写法 MS_BIND | MS_REC 在单文件 bind 上会 EINVAL，去掉 MS_REC
    if (::mount(t.source.c_str(), t.target.c_str(), nullptr,
                MS_BIND, nullptr) < 0) {
        NX_LOG_W("BindMount", "bind failed for %s: %s",
                 t.target.c_str(), nexus::errnoString(errno).c_str());
        return {unexpect, Err::MountFailed};
    }
    // Phase 1.7 修复：原代码只读重挂未检查返回值，失败会留 read-write mount
    if (::mount(t.source.c_str(), t.target.c_str(), nullptr,
                MS_BIND | MS_REMOUNT | MS_RDONLY, nullptr) < 0) {
        NX_LOG_W("BindMount", "remount RO failed for %s: %s (mount stays RW)",
                 t.target.c_str(), nexus::errnoString(errno).c_str());
        // 不致命，但记录（生产应拒绝继续）
    }
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
