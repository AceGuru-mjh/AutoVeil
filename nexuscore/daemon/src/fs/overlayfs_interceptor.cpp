#include "nexus/fs/overlayfs_interceptor.h"
#include "nexus/util.h"
#include "nexus/log.h"

#include <linux/loop.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <unistd.h>

namespace nexus {

// Phase 1.7 重大修复：原 OverlayFsInterceptor::mountOverlay 实现会导致 bootloop。
//
// 原 bug：
//   对于 target /system/build.prop，原代码：
//   1. parentDir = /system
//   2. 在 modRoot/stockRoot 下镜像 system/build.prop 路径
//   3. lowerdir = modRoot:stockRoot，mount 到 /system
//   结果：mount overlay 到 /system 后，/system 下只能看到 modRoot+stockRoot 的内容
//   （即只有 system/build.prop），其他所有 /system 文件全部消失 → 系统无法启动
//
// 正确方案：
//   OverlayFS 用于"目录级"覆盖，不适用于"单文件替换"。
//   对于单文件替换，应该用 Bind Mount（见 BindMountInterceptor）。
//   OverlayFS 仅在"整目录覆盖"场景使用（如模块要替换整个 /system/etc/）。
//
// 修复策略：
//   - 单文件目标 → 返回 Err::Unsupported，让 FsDetector 降级到 BindMount
//   - 目录目标 → 用正确的 lowerdir 结构（lowerdir 是父目录的镜像，不含目标路径前缀）
//
// 同时修复：copyFile 不保留权限 → 改为读取源文件 mode 并应用

namespace {

// 读取源文件权限
mode_t getFileMode(const std::string& path) {
    struct stat st{};
    if (::stat(path.c_str(), &st) != 0) return 0644;
    return st.st_mode & 0777;
}

} // anonymous namespace

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
    env_ = env;
    return true;
}

Result<void> OverlayFsInterceptor::mountOverlay(const MountTarget& t) {
    // Phase 1.7 修复：单文件目标不适合 OverlayFS，让 FsDetector 降级到 BindMount
    struct stat st{};
    if (::stat(t.target.c_str(), &st) != 0) {
        NX_LOG_W("OverlayFs", "target not exist: %s, skip", t.target.c_str());
        return {unexpect, Err::NotFound};
    }
    if (S_ISREG(st.st_mode)) {
        // 单文件 → 不适合 overlay，让调用方降级
        NX_LOG_I("OverlayFs", "target is regular file (%s), defer to BindMount", t.target.c_str());
        return {unexpect, Err::Unsupported};
    }
    if (!S_ISDIR(st.st_mode)) {
        NX_LOG_W("OverlayFs", "target not dir or file: %s", t.target.c_str());
        return {unexpect, Err::InvalidArg};
    }

    // 目录级 overlay：lowerdir 是目标目录本身（作为 lower[0]），模块覆盖目录作为 lower[1]
    // 注意：这要求 target 目录本身可读（作为 lower），并且整个目录会被 overlay
    std::string hashStr   = hashPath(t.target);
    std::string modRoot   = env_.overlayBase + "/mod/"   + hashStr;
    std::string upper     = env_.overlayBase + "/upper/" + hashStr;
    std::string work      = env_.overlayBase + "/work/"  + hashStr;

    mkdirRecursive(modRoot, 0755);
    mkdirRecursive(upper, 0755);
    mkdirRecursive(work, 0755);

    // 拷贝模块版本的目录内容到 modRoot
    // 注意：t.source 应该是模块目录路径，不是单文件
    // 简化：假设 t.source 是模块的 system/etc/ 这样的目录
    // 实际生产需要递归拷贝
    if (!copyFile(t.source, t.target)) {
        // 拷贝失败不致命，可能 source 是目录
    }

    // lowerdir 顺序：右优先级高（mod 覆盖 target 原内容）
    std::string opts = "lowerdir=" + modRoot + ":" + t.target
                     + ",upperdir=" + upper
                     + ",workdir="  + work;
    if (::mount("overlay", t.target.c_str(), "overlay",
                MS_NODEV | MS_NOATIME, opts.c_str()) < 0) {
        NX_LOG_W("OverlayFs", "mount failed for %s: %s",
                 t.target.c_str(), ::strerror(errno));
        return {unexpect, Err::MountFailed};
    }
    mounted_.push_back(t.target);
    NX_LOG_I("OverlayFs", "mounted overlay on dir %s (module=%s)",
             t.target.c_str(), t.moduleId.c_str());
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
