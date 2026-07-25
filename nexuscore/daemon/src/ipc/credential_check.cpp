#include "nexus/ipc/credential_check.h"
#include "nexus/util.h"
#include "nexus/log.h"

#include <pwd.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>

namespace nexus::ipc {

Result<PeerCredential> CredentialCheck::readPeer(int fd) {
    struct ucred uc;
    socklen_t len = sizeof(uc);
    if (::getsockopt(fd, SOL_SOCKET, SO_PEERCRED, &uc, &len) < 0) {
        return std::unexpected(Err::IoError);
    }
    PeerCredential p;
    p.pid = uc.pid;
    p.uid = uc.uid;
    p.gid = uc.gid;

    // 读 SELinux context
    std::string attrPath = "/proc/" + std::to_string(uc.pid) + "/attr/current";
    auto ctx = readFile(attrPath);
    if (ctx) p.selinuxContext = trim(*ctx);

    // 读 cmdline 拿包名
    std::string cmdPath = "/proc/" + std::to_string(uc.pid) + "/cmdline";
    auto cmd = readFile(cmdPath);
    if (cmd) {
        // cmdline 用 \0 分隔参数，取第一个
        size_t nul = cmd->find('\0');
        p.packageName = nul == std::string::npos ? *cmd : cmd->substr(0, nul);
    }

    return p;
}

Result<void> CredentialCheck::authorize(const PeerCredential& peer) {
    // 1) UID 校验：必须是系统或 Nexus Manager 的 UID
    // Manager UID 在启动期由 IpcServer 通过 ManagerUidResolver 解析，写入全局变量
    // 这里简化：允许 UID >= 10000（普通 app）+ 1000 (system)
    if (peer.uid != 1000 && peer.uid < 10000) {
        NX_LOG_W("Cred", "rejected uid=%d (< 10000 and not system)", peer.uid);
        return std::unexpected(Err::Unauthorized);
    }

    // 2) 包名校验（cmdline 第一个参数）
    // Manager 包名为 com.nexus.manager 或 com.nexus.manager.debug
    if (peer.packageName != "com.nexus.manager" &&
        peer.packageName != "com.nexus.manager.debug") {
        NX_LOG_W("Cred", "rejected pkg=%s", peer.packageName.c_str());
        return std::unexpected(Err::Unauthorized);
    }

    // 3) SELinux 域校验（整改 #4：原 find("u:r:untrusted_app:s0") 匹配不到
    //    untrusted_app_30:s0:c... 的实际 context，所有合法 Manager 被拒）
    //    改为前缀匹配
    auto inDomain = [](const std::string& ctx, const std::string& prefix) {
        return ctx.rfind(prefix, 0) == 0;
    };
    if (!inDomain(peer.selinuxContext, "u:r:untrusted_app") &&
        !inDomain(peer.selinuxContext, "u:r:platform_app") &&
        !inDomain(peer.selinuxContext, "u:r:system_app")) {
        NX_LOG_W("Cred", "rejected selinux ctx=%s", peer.selinuxContext.c_str());
        return std::unexpected(Err::Unauthorized);
    }

    // 4) APK 签名指纹校验（防伪 Manager）
    // MVP 简化：跳过完整签名校验，依赖前 3 步（UID + 包名 + SELinux 域）
    // 生产实现应 locateApk + sha256OfFile + 对比 EXPECTED_MANAGER_SIG
    // 详细见 spec-01 §10.2

    return {};
}

} // namespace nexus::ipc
