#pragma once

#include "nexus/types.h"
#include "nexus/event_bus.h"
#include "nexus/ipc/codec.h"
#include "nexus/ipc/credential_check.h"
#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace nexus {

// 前向声明，避免循环 include
class DaemonCore;

namespace ipc {

// IPC 服务端
//
// 职责：
// - 监听 UDS /dev/socket/nexusd.sock
// - 接受连接 → 凭证校验 → 启动会话线程
// - 会话线程：读 Envelope → 分发到 handlers → 写 Response
// - 支持事件推送（订阅 logs / su_request）
//
// 整改 #5：socket 权限使用 ManagerUidResolver 动态 chown，避免 catch-22。
//
// Phase B 改进：增加 core_ 指针，让 handlers 能调用 DaemonCore 真实方法。
//
// Phase 1.4 修复：
// - stop() 改为 join 而非 detach，避免 UAF
// - 每个会话的 fd 写操作加锁，防止并发写交叉
// - 跟踪所有 cfd，stop() 时 shutdown 让 readFrame 解除阻塞
class IpcServer {
public:
    IpcServer(EventBus& bus, DaemonCore* core = nullptr)
        : bus_(bus), core_(core) {}
    ~IpcServer() { stop(); }

    // 注入 DaemonCore（在 main.cpp 创建 core 后调用）
    void setCore(DaemonCore* core) { core_ = core; }

    // 启动服务（监听 + accept 线程）
    Result<void> start(std::string_view socketPath);

    // 停止服务（关闭 listen fd + shutdown 所有 cfd + join 所有会话线程）
    void stop();

    bool isRunning() const { return running_.load(); }

private:
    EventBus& bus_;
    DaemonCore* core_ = nullptr;
    int listenFd_ = -1;
    std::atomic<bool> running_{false};
    std::thread acceptThread_;
    std::vector<std::thread> sessionThreads_;
    std::mutex sessionMu_;             // 保护 sessionThreads_ / fdWriteMus_

    // 每个 fd 一个写锁，防止 pushEvent 与 handleClient 的 writeFrame 并发写交叉
    std::unordered_map<int, std::unique_ptr<std::mutex>> fdWriteMus_;
    std::mutex fdWriteMuMapMu_;        // 保护 fdWriteMus_ map 本身

    void acceptLoop();
    void handleClient(int cfd, const PeerCredential& peer);

    // 推送事件到指定 client fd（线程安全）
    void pushEvent(int fd, const Event& ev);

    // 获取 / 释放 fd 的写锁
    std::mutex* getFdWriteMu(int fd);
    void releaseFdWriteMu(int fd);
};

} // namespace nexus::ipc
} // namespace nexus
