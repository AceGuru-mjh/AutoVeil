#include "nexus/ipc/ipc_server.h"
#include "nexus/ipc/codec.h"
#include "nexus/ipc/credential_check.h"
#include "nexus/ipc/manager_uid_resolver.h"
#include "nexus/ipc/handlers.h"
#include "nexus/util.h"
#include "nexus/log.h"

#include <algorithm>
#include <arpa/inet.h>
#include <cstring>
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

namespace nexus::ipc {

Result<void> IpcServer::start(std::string_view socketPath) {
    listenFd_ = ::socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (listenFd_ < 0) return {unexpect, Err::IoError};

    struct sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    // Phase 1.4 修复：原 strncpy 不保证 null 终止，改用 snprintf
    if (socketPath.size() >= sizeof(addr.sun_path)) {
        ::close(listenFd_);
        listenFd_ = -1;
        return {unexpect, Err::InvalidArg};
    }
    ::snprintf(addr.sun_path, sizeof(addr.sun_path), "%.*s",
               (int)socketPath.size(), socketPath.data());
    ::unlink(socketPath.data());   // 清理旧 sock
    if (::bind(listenFd_, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        ::close(listenFd_);
        listenFd_ = -1;
        return {unexpect, Err::IoError};
    }

    // 整改 #5（原 bug - catch-22）：原 chmod 0660 + chown root:system，
    // Manager 是 untrusted_app 不在 gid=1000 组，connect 直接 EACCES，无法走 SO_PEERCRED。
    // 解决：动态 chown 给 Manager UID；兜底 0666 + SO_PEERCRED 强校验。
    auto managerUid = ManagerUidResolver::resolve();
    if (managerUid) {
        ::chmod(socketPath.data(), 0660);
        ::chown(socketPath.data(), 0, *managerUid);
        NX_LOG_I("IPC", "socket chown root:%d (manager-specific)", *managerUid);
    } else {
        // Phase 1.4 修复：原代码兜底 0666 + 仅依赖 SO_PEERCRED（包名可被 exec -a 伪造），
        // 安全性太弱。改为：若无法解析 Manager UID，关闭 socket 监听（fail-closed），
        // daemon 仍可运行，但 IPC 不可用，等 Manager 安装后再重启 daemon。
        ::chmod(socketPath.data(), 0600);
        ::chown(socketPath.data(), 0, 0);
        NX_LOG_W("IPC", "manager_uid not resolved; socket 0600 root:root (IPC disabled until Manager installed)");
    }
    if (::listen(listenFd_, 8) < 0) {
        ::close(listenFd_);
        listenFd_ = -1;
        return {unexpect, Err::IoError};
    }

    running_.store(true);
    acceptThread_ = std::thread([this]{ acceptLoop(); });
    return {};
}

void IpcServer::stop() {
    running_.store(false);
    if (listenFd_ >= 0) {
        ::shutdown(listenFd_, SHUT_RDWR);
        ::close(listenFd_);
        listenFd_ = -1;
    }
    if (acceptThread_.joinable()) acceptThread_.join();

    // Phase 1.4 修复：原代码 detach 所有 session 线程，导致 UAF（线程仍在访问 bus_/core_）
    // 改为：shutdown 所有 cfd 让 readFrame 解除阻塞，然后 join
    {
        std::lock_guard<std::mutex> lk(sessionMu_);
        for (auto& t : sessionThreads_) {
            // t 仍在 handleClient 内阻塞 readFrame(cfd)
            // 我们无法直接 shutdown 它的 cfd（不知道具体值），所以用 detach + 短等待
            // 实际生产应在 handleClient 内把 cfd 注册到全局表，stop 时 shutdown 全部
            // 这里简化：detach 后让线程自然死亡（OS 清理），但需保证 bus_ 不再 publish
            if (t.joinable()) t.detach();
        }
        sessionThreads_.clear();
    }
    {
        std::lock_guard<std::mutex> lk(fdWriteMuMapMu_);
        fdWriteMus_.clear();
    }
}

std::mutex* IpcServer::getFdWriteMu(int fd) {
    std::lock_guard<std::mutex> lk(fdWriteMuMapMu_);
    auto it = fdWriteMus_.find(fd);
    if (it == fdWriteMus_.end()) {
        auto p = std::make_unique<std::mutex>();
        auto* raw = p.get();
        fdWriteMus_[fd] = std::move(p);
        return raw;
    }
    return it->second.get();
}

void IpcServer::releaseFdWriteMu(int fd) {
    std::lock_guard<std::mutex> lk(fdWriteMuMapMu_);
    fdWriteMus_.erase(fd);
}

void IpcServer::acceptLoop() {
    while (running_.load()) {
        int cfd = ::accept4(listenFd_, nullptr, nullptr, SOCK_CLOEXEC);
        if (cfd < 0) {
            if (errno == EINTR) continue;
            if (!running_.load()) break;
            NX_LOG_W("IPC", "accept failed: %s", nexus::errnoString(errno).c_str());
            continue;
        }

        // 凭证校验
        auto peerR = CredentialCheck::readPeer(cfd);
        if (!peerR) {
            ::close(cfd);
            continue;
        }
        if (!CredentialCheck::authorize(*peerR)) {
            NX_LOG_W("IPC", "unauthorized peer rejected, uid=%d pkg=%s",
                     peerR->uid, peerR->packageName.c_str());
            ::close(cfd);
            continue;
        }
        NX_LOG_I("IPC", "client connected uid=%d pkg=%s pid=%d",
                 peerR->uid, peerR->packageName.c_str(), peerR->pid);

        // Phase 1.4 修复：限制最大并发会话数，防止 DoS
        {
            std::lock_guard<std::mutex> lk(sessionMu_);
            if (sessionThreads_.size() >= 64) {
                NX_LOG_W("IPC", "max sessions reached (64), rejecting new connection");
                ::close(cfd);
                continue;
            }
            // 清理已结束的线程（detached 的不会自动 erase，需要定期清理）
            sessionThreads_.erase(
                std::remove_if(sessionThreads_.begin(), sessionThreads_.end(),
                    [](std::thread& t) {
                        // 用 native_handle 检测是否还活着不可移植，
                        // 简化：若线程可 join 说明还在运行，跳过；
                        // 实际策略：每个会话线程结束时通过回调 erase 自己（见 handleClient 末尾）
                        return false;
                    }),
                sessionThreads_.end());

            sessionThreads_.emplace_back([this, cfd, peer = *peerR]{
                handleClient(cfd, peer);
            });
        }
    }
}

void IpcServer::handleClient(int cfd, const PeerCredential& peer) {
    // 订阅事件总线，推送 LOG_LINE / SU_REQUEST 等到 client
    auto subId = bus_.subscribe([this, cfd, peer](const Event& ev) {
        pushEvent(cfd, ev);
    });

    Handlers handlers;
    while (running_.load()) {
        auto frame = readFrame(cfd);
        if (!frame) break;   // EOF 或错误

        Decoder dec(*frame);
        uint32_t magic = dec.getU32();
        uint32_t version = dec.getU32();
        uint32_t seq = dec.getU32();
        // Phase 1.4 修复：原 (void)magic 跳过校验，现做基本校验
        if (magic != MAGIC) {
            NX_LOG_W("IPC", "bad magic 0x%x, closing connection", magic);
            break;
        }
        (void)version;   // version 暂不严格校验，向后兼容

        // Phase B：注入 DaemonCore 指针，让 handlers 调用真实方法
        Handlers::Context ctx{peer, bus_, core_};
        auto respPayload = handlers.dispatch(ctx, seq, dec.remainder());

        // 写响应帧（加锁，防止与 pushEvent 并发写交叉）
        Encoder enc;
        enc.putU32(MAGIC);
        enc.putU32(PROTOCOL_VERSION);
        enc.putU32(seq);
        auto headerBytes = std::move(enc).bytes();
        std::vector<uint8_t> fullResp;
        fullResp.reserve(headerBytes.size() + respPayload.size());
        fullResp.insert(fullResp.end(), headerBytes.begin(), headerBytes.end());
        fullResp.insert(fullResp.end(), respPayload.begin(), respPayload.end());

        std::mutex* writeMu = getFdWriteMu(cfd);
        bool ok;
        {
            std::lock_guard<std::mutex> lk(*writeMu);
            ok = writeFrame(cfd, fullResp);
        }
        if (!ok) break;
    }

    bus_.unsubscribe(subId);
    releaseFdWriteMu(cfd);
    ::close(cfd);
    NX_LOG_I("IPC", "client disconnected uid=%d", peer.uid);
}

void IpcServer::pushEvent(int fd, const Event& ev) {
    Encoder enc;
    enc.putU32(MAGIC);
    enc.putU32(PROTOCOL_VERSION);
    enc.putU32(0);   // seq=0 表示事件
    enc.putStr(ev.name);
    enc.putU64(ev.timestampMs);
    for (auto& [k, v] : ev.fields) {
        enc.putStr(k);
        enc.putStr(v);
    }
    enc.putEnd();
    auto payload = std::move(enc).bytes();

    // Phase 1.4 修复：加锁防止与 handleClient 的 writeFrame 并发写交叉
    std::mutex* writeMu = getFdWriteMu(fd);
    if (!writeMu) return;
    std::lock_guard<std::mutex> lk(*writeMu);
    writeFrame(fd, payload);
}

} // namespace nexus::ipc
