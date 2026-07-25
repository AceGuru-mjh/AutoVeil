#include "nexus/ipc/manager_uid_resolver.h"
#include "nexus/util.h"
#include "nexus/log.h"

#include <pwd.h>
#include <sys/stat.h>

namespace nexus::ipc {

std::optional<int> ManagerUidResolver::resolve() {
    // 1) 优先读 /data/adb/nexuscore/manager_uid（Magisk 模块 post-fs-data 写入）
    auto content = readFile("/data/adb/nexuscore/manager_uid");
    if (content) {
        std::string s = trim(*content);
        if (!s.empty()) {
            try {
                int uid = std::stoi(s);
                if (uid > 0) {
                    NX_LOG_I("ManagerUid", "resolved from manager_uid file: %d", uid);
                    return uid;
                }
            } catch (...) {}
        }
    }

    // 2) 通过 pm path 查找 APK，stat 得到 UID
    //    注意：pm path 命令需要 root，daemon 本身是 root，所以可执行
    auto r = execCommand("pm path com.nexus.manager 2>/dev/null", 5);
    if (r.exitCode == 0 && !r.stdout_.empty()) {
        // 输出格式：package:/data/app/~~xxx/com.nexus.manager-yyy/base.apk
        std::string out = trim(r.stdout_);
        const std::string prefix = "package:";
        if (out.rfind(prefix, 0) == 0) {
            std::string apkPath = out.substr(prefix.size());
            // 处理多个 base.apk 路径（split APK）
            size_t nl = apkPath.find('\n');
            if (nl != std::string::npos) apkPath = apkPath.substr(0, nl);
            struct stat st{};
            if (::stat(apkPath.c_str(), &st) == 0) {
                NX_LOG_I("ManagerUid", "resolved from apk stat: %d (path=%s)",
                         (int)st.st_uid, apkPath.c_str());
                return (int)st.st_uid;
            }
        }
    }

    // 3) 兜底：试 debug 包名
    auto r2 = execCommand("pm path com.nexus.manager.debug 2>/dev/null", 5);
    if (r2.exitCode == 0 && !r2.stdout_.empty()) {
        std::string out = trim(r2.stdout_);
        const std::string prefix = "package:";
        if (out.rfind(prefix, 0) == 0) {
            std::string apkPath = out.substr(prefix.size());
            size_t nl = apkPath.find('\n');
            if (nl != std::string::npos) apkPath = apkPath.substr(0, nl);
            struct stat st{};
            if (::stat(apkPath.c_str(), &st) == 0) {
                NX_LOG_I("ManagerUid", "resolved debug uid: %d", (int)st.st_uid);
                return (int)st.st_uid;
            }
        }
    }

    NX_LOG_W("ManagerUid", "could not resolve manager UID");
    return std::nullopt;
}

} // namespace nexus::ipc
