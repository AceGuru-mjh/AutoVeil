#include "nexus/ipc/ipc_server.h"
#include "nexus/ipc/codec.h"
#include "nexus/ipc/credential_check.h"
#include "nexus/ipc/manager_uid_resolver.h"
#include "nexus/ipc/handlers.h"
#include "nexus/util.h"
#include "nexus/log.h"

#include <arpa/inet.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

namespace nexus::ipc {

Result<void> IpcServer::start(std::string_view socketPath) {
    listenFd_ = ::socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (listenFd_ < 0) return std::unexpected(Err::IoError);

    struct sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    ::strncpy(addr.sun_path, socketPath.data(), sizeof(addr.sun_path) - 1);
    ::unlink(socketPath.data());   // 清理旧 sock
    if (::bind(listenFd_, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        ::close(listenFd_);
        listenFd_ = -1;
        return std::unexpected(Err::IoError);
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
        ::chmod(socketPath.data(), 0666);
        NX_LOG_W("IPC", "manager_uid file missing; using 0666 + SO_PEERCRED");
    }

    if (::listen(listenFd_, 8) < 0) {
        ::close(listenFd_);
        listenFd_ = -1;
        return std::unexpected(Err::IoError);
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
    std::lock_guard<std::mutex> lk(sessionMu_);
    for (auto& t : sessionThreads_) {
        if (t.joinable()) t.detach();   // 不等待，让会话线程自己结束
    }
    sessionThreads_.clear();
}

void IpcServer::acceptLoop() {
    while (running_.load()) {
        int cfd = ::accept4(listenFd_, nullptr, nullptr, SOCK_CLOEXEC);
        if (cfd < 0) {
            if (errno == EINTR) continue;
            if (!running_.load()) break;
            NX_LOG_W("IPC", "accept failed: %s", ::strerror(errno));
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

        std::lock_guard<std::mutex> lk(sessionMu_);
        sessionThreads_.emplace_back([this, cfd, peer = *peerR]{
            handleClient(cfd, peer);
        });
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
        // 简化：MVP 跳过 magic/version 严格校验
        (void)magic; (void)version;

        Handlers::Context ctx{peer, bus_, /* core */ nullptr};
        // 注意：实际生产中 core 应该通过 IpcServer 注入，这里简化
        // 完整实现见 daemon_core 注入
        auto respPayload = handlers.dispatch(ctx, seq, std::vector<uint8_t>(
            frame->begin() + dec.pos_, frame->end()));

        // 写响应帧
        Encoder enc;
        enc.putU32(MAGIC);
        enc.putU32(PROTOCOL_VERSION);
        enc.putU32(seq);
        // 把 dispatch 返回的 payload 拼接进去
        auto headerBytes = std::move(enc).bytes();
        std::vector<uint8_t> fullResp;
        fullResp.reserve(headerBytes.size() + respPayload.size());
        fullResp.insert(fullResp.end(), headerBytes.begin(), headerBytes.end());
        fullResp.insert(fullResp.end(), respPayload.begin(), respPayload.end());
        if (!writeFrame(cfd, fullResp)) break;
    }

    bus_.unsubscribe(subId);
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
    writeFrame(fd, std::move(enc).bytes());
}

} // namespace nexus::ipc
