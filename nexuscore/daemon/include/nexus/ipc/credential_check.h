#pragma once

#include "nexus/types.h"
#include <string>

namespace nexus::ipc {

struct PeerCredential {
    int pid = -1;
    int uid = -1;
    int gid = -1;
    std::string selinuxContext;
    std::string packageName;   // 从 /proc/<pid>/cmdline
};

class CredentialCheck {
public:
    // 读 SO_PEERCRED + SELinux context + cmdline
    static Result<PeerCredential> readPeer(int fd);

    // 校验：UID 白名单 + 包名 + SELinux 域 + APK 签名指纹
    // 整改 #4：原写法 find("u:r:untrusted_app:s0") 匹配不到 untrusted_app_30:s0:c...
    //          改为前缀匹配。
    // 整改 #5：原写法 chmod 0660 + chown root:system catch-22，由 IpcServer 解决。
    static Result<void> authorize(const PeerCredential& peer);
};

} // namespace nexus::ipc
