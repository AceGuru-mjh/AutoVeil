#pragma once

#include "nexus/types.h"
#include "nexus/event_bus.h"
#include "nexus/ipc/codec.h"
#include "nexus/ipc/credential_check.h"
#include <atomic>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace nexus::ipc {

// IPC 服务端
//
// 职责：
// - 监听 UDS /dev/socket/nexusd.sock
// - 接受连接 → 凭证校验 → 启动会话线程
// - 会话线程：读 Envelope → 分发到 handlers → 写 Response
// - 支持事件推送（订阅 logs / su_request）
//
// 整改 #5：socket 权限使用 ManagerUidResolver 动态 chown，避免 catch-22。
class IpcServer {
public:
    IpcServer(EventBus& bus) : bus_(bus) {}
    ~IpcServer() { stop(); }

    // 启动服务（监听 + accept 线程）
    Result<void> start(std::string_view socketPath);

    // 停止服务（关闭 listen fd + 等待所有会话线程退出）
    void stop();

    bool isRunning() const { return running_.load(); }

private:
    EventBus& bus_;
    int listenFd_ = -1;
    std::atomic<bool> running_{false};
    std::thread acceptThread_;
    std::vector<std::thread> sessionThreads_;
    std::mutex sessionMu_;

    void acceptLoop();
    void handleClient(int cfd, const PeerCredential& peer);

    // 推送事件到指定 client fd
    void pushEvent(int fd, const Event& ev);
};

} // namespace nexus::ipc
