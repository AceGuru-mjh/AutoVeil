#pragma once

#include "nexus/types.h"
#include "nexus/util.h"
#include <string>
#include <vector>
#include <mutex>
#include <cstdint>

namespace nexus {

/// SU 授权守护进程
///
/// Phase 3：NexusCore 自研的 su 授权系统，与 Magisk/KSU/APatch 同级。
///
/// 工作流程：
/// 1. 应用调用 /system/bin/su → su 客户端二进制
/// 2. su 客户端通过 UDS 连接到 nexusd 的 su socket (/dev/socket/nexus_su.sock)
/// 3. su 客户端发送请求：uid, pid, command
/// 4. nexusd SuDaemon 收到请求：
///    a. 查询 su_policy.json 是否有持久化策略
///    b. 若无策略，通过 IPC 推送 SuRequestEvent 给 Manager
///    c. Manager 弹出 SuRequestActivity 让用户授权
///    d. 用户响应后通过 IPC 返回策略
/// 5. SuDaemon 根据策略决定是否 fork 出 root shell
/// 6. fork 的 root shell 通过 PTY 与 su 客户端通信
///
/// 不抄 Magisk 的部分：
/// - 不用 AIDL（Magisk 用 IRootServices.aidl）
/// - 不用 SELinux domain transition（Magisk 用 magiskclient → magisk daemon）
/// - 用纯 UDS + protobuf-style 二进制协议
/// - 策略持久化用 JSON（Magisk 用 SQLite）
///
/// 安全模型：
/// - su 二进制 setuid root，但仅做"连接到 daemon + 透传 stdin/stdout"
/// - 真正的 root shell 由 daemon fork（daemon 已经是 root）
/// - 客户端无法直接获取 root，必须经过 daemon 授权
class SuDaemon {
public:
    /// SU 策略
    enum class Policy : int {
        Deny = 0,
        Allow = 1,
        AllowOnce = 2,
    };

    /// SU 应用信息（持久化）
    struct SuApp {
        std::string packageName;
        uint32_t uid = 0;
        Policy policy = Policy::Deny;
        uint64_t lastRequestMs = 0;
        uint32_t requestCount = 0;
        uint32_t timeoutSec = 0;   // 0=永久
    };

    /// SU 日志条目
    struct SuLogEntry {
        uint64_t timestampMs = 0;
        std::string packageName;
        uint32_t uid = 0;
        bool granted = false;
        std::string command;
    };

    /// SU 请求（来自 su 客户端）
    struct SuRequest {
        uint32_t uid = 0;
        uint32_t pid = 0;
        std::string packageName;
        std::string command;
        // 客户端 socket fd（用于回传 shell PTY）
        int clientFd = -1;
    };

    SuDaemon() = default;

    /// 处理一个 SU 请求（阻塞，直到用户响应或超时）
    ///
    /// @return true 表示授权（fork root shell），false 表示拒绝
    bool handleRequest(const SuRequest& req);

    /// 加载持久化策略
    Result<void> loadPolicy();

    /// 保存策略到 /data/adb/nexuscore/su_policy.json
    Result<void> savePolicy();

    /// 设置指定应用的策略
    bool setPolicy(const std::string& pkg, uint32_t uid, Policy policy, uint32_t timeoutSec);

    /// 列出所有已授权应用
    std::vector<SuApp> listApps() const;

    /// 列出 SU 日志
    std::vector<SuLogEntry> listLogs() const;

    /// 清除日志
    void clearLogs();

    /// 监听 su socket，接受连接
    ///
    /// @param socketPath UDS 路径，默认 /dev/socket/nexus_su.sock
    Result<void> startListening(const std::string& socketPath);

    /// 停止监听
    void stopListening();

private:
    mutable std::mutex mu_;
    std::vector<SuApp> apps_;
    std::vector<SuLogEntry> logs_;
    int listenFd_ = -1;
    bool running_ = false;

    /// 查询指定 uid 的策略
    /// @return Policy::Deny 表示无策略或拒绝
    Policy lookupPolicy(uint32_t uid, const std::string& pkg) const;

    /// 检查策略是否已过期
    bool isPolicyExpired(const SuApp& app) const;

    /// 记录 SU 日志
    void addLog(const SuRequest& req, bool granted);

    /// Fork 一个 root shell 并通过 PTY 与客户端通信
    ///
    /// 1. forkpty 创建 PTY
    /// 2. child: setuid(0) + setgid(0) + exec /system/bin/sh
    /// 3. parent: 在 clientFd 与 PTY 之间双向透传字节
    void forkRootShell(const SuRequest& req);

    /// 解析 /proc/<pid>/cmdline 拿包名
    static std::string resolvePackageName(uint32_t pid);

    /// 极简 JSON 序列化/反序列化
    std::string serializeApps() const;
    Result<void> parseApps(const std::string& json);
};

/// su 客户端入口（独立二进制 /system/bin/su）
///
/// 这个函数会被编译为单独的 su 二进制：
///   1. 检查调用者身份（UID/PID）
///   2. 连接到 /dev/socket/nexus_su.sock
///   3. 发送 SuRequest
///   4. 等待 daemon 响应
///   5. 若授权，双向透传 stdin/stdout 与 daemon PTY
int suClientMain(int argc, char** argv);

} // namespace nexus
