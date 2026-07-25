// SU Daemon 实现
//
// Phase 3：NexusCore 自研的 su 授权系统。

#include "nexus/su_daemon.h"
#include "nexus/util.h"
#include "nexus/log.h"
#include "nexus/event_bus.h"

#include <chrono>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <unistd.h>
#ifdef __ANDROID__
#include <pty.h>
#else
// host 测试用 forkpty 替代
#include <stdlib.h>
#endif

namespace nexus {

namespace {

/// 当前时间戳（毫秒）
uint64_t nowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}

/// 检查超时是否过期
bool isExpired(uint64_t lastMs, uint32_t timeoutSec) {
    if (timeoutSec == 0) return false;   // 永久策略
    uint64_t elapsed = (nowMs() - lastMs) / 1000;
    return elapsed >= timeoutSec;
}

} // anonymous namespace

Result<void> SuDaemon::loadPolicy() {
    std::lock_guard<std::mutex> lk(mu_);
    auto content = readFile("/data/adb/nexuscore/su_policy.json");
    if (!content) {
        return {};   // 文件不存在视为空策略
    }
    return parseApps(*content);
}

Result<void> SuDaemon::savePolicy() {
    std::lock_guard<std::mutex> lk(mu_);
    std::string json = serializeApps();
    if (!writeFile("/data/adb/nexuscore/su_policy.json", json, 0600)) {
        return {unexpect, Err::IoError};
    }
    return {};
}

std::string SuDaemon::serializeApps() const {
    // 极简 JSON 序列化
    std::string s = "{\"apps\":[";
    for (size_t i = 0; i < apps_.size(); ++i) {
        if (i > 0) s += ",";
        s += "{";
        s += "\"package_name\":\"" + apps_[i].packageName + "\",";
        s += "\"uid\":" + std::to_string(apps_[i].uid) + ",";
        s += "\"policy\":" + std::to_string((int)apps_[i].policy) + ",";
        s += "\"last_request_ms\":" + std::to_string(apps_[i].lastRequestMs) + ",";
        s += "\"request_count\":" + std::to_string(apps_[i].requestCount) + ",";
        s += "\"timeout_sec\":" + std::to_string(apps_[i].timeoutSec);
        s += "}";
    }
    s += "]}";
    return s;
}

Result<void> SuDaemon::parseApps(const std::string& json) {
    // 极简 JSON 解析（不依赖 nlohmann/json）
    // 这里复用 module_loader.cpp 的 JsonParser 思路
    // 简化：用字符串查找
    apps_.clear();

    size_t pos = 0;
    while ((pos = json.find("\"package_name\":\"", pos)) != std::string::npos) {
        SuApp app;
        size_t start = pos + 16;
        size_t end = json.find('"', start);
        if (end == std::string::npos) break;
        app.packageName = json.substr(start, end - start);
        pos = end;

        // 找 uid
        auto findField = [&](const std::string& key) -> std::string {
            size_t p = json.find("\"" + key + "\":", pos);
            if (p == std::string::npos) return "";
            p += key.size() + 3;
            size_t e = p;
            while (e < json.size() && (json[e] == '-' || (json[e] >= '0' && json[e] <= '9'))) ++e;
            return json.substr(p, e - p);
        };

        auto uidStr = findField("uid");
        if (!uidStr.empty()) {
            errno = 0;
            app.uid = (uint32_t)std::strtoul(uidStr.c_str(), nullptr, 10);
        }
        auto policyStr = findField("policy");
        if (!policyStr.empty()) {
            app.policy = (Policy)std::atoi(policyStr.c_str());
        }
        auto lastStr = findField("last_request_ms");
        if (!lastStr.empty()) {
            app.lastRequestMs = std::strtoull(lastStr.c_str(), nullptr, 10);
        }
        auto countStr = findField("request_count");
        if (!countStr.empty()) {
            app.requestCount = (uint32_t)std::strtoul(countStr.c_str(), nullptr, 10);
        }
        auto timeoutStr = findField("timeout_sec");
        if (!timeoutStr.empty()) {
            app.timeoutSec = (uint32_t)std::strtoul(timeoutStr.c_str(), nullptr, 10);
        }

        apps_.push_back(app);
    }
    return {};
}

bool SuDaemon::isPolicyExpired(const SuApp& app) const {
    return isExpired(app.lastRequestMs, app.timeoutSec);
}

SuDaemon::Policy SuDaemon::lookupPolicy(uint32_t uid, const std::string& pkg) const {
    for (auto& app : apps_) {
        if (app.uid == uid && app.packageName == pkg) {
            if (isPolicyExpired(app)) {
                return Policy::Deny;   // 过期视为拒绝
            }
            return app.policy;
        }
    }
    return Policy::Deny;
}

bool SuDaemon::setPolicy(const std::string& pkg, uint32_t uid, Policy policy, uint32_t timeoutSec) {
    std::lock_guard<std::mutex> lk(mu_);
    // 查找已有
    for (auto& app : apps_) {
        if (app.uid == uid && app.packageName == pkg) {
            app.policy = policy;
            app.timeoutSec = timeoutSec;
            app.lastRequestMs = nowMs();
            return savePolicy().has_value();
        }
    }
    // 新增
    SuApp app;
    app.packageName = pkg;
    app.uid = uid;
    app.policy = policy;
    app.timeoutSec = timeoutSec;
    app.lastRequestMs = nowMs();
    app.requestCount = 0;
    apps_.push_back(app);
    return savePolicy().has_value();
}

std::vector<SuDaemon::SuApp> SuDaemon::listApps() const {
    std::lock_guard<std::mutex> lk(mu_);
    // 过滤已过期的
    std::vector<SuApp> result;
    for (auto& app : apps_) {
        if (!isPolicyExpired(app)) {
            result.push_back(app);
        }
    }
    return result;
}

std::vector<SuDaemon::SuLogEntry> SuDaemon::listLogs() const {
    std::lock_guard<std::mutex> lk(mu_);
    return logs_;
}

void SuDaemon::clearLogs() {
    std::lock_guard<std::mutex> lk(mu_);
    logs_.clear();
}

void SuDaemon::addLog(const SuRequest& req, bool granted) {
    std::lock_guard<std::mutex> lk(mu_);
    SuLogEntry entry;
    entry.timestampMs = nowMs();
    entry.packageName = req.packageName;
    entry.uid = req.uid;
    entry.granted = granted;
    entry.command = req.command;
    logs_.push_back(entry);
    // 限制日志数量
    if (logs_.size() > 1000) {
        logs_.erase(logs_.begin(), logs_.begin() + (logs_.size() - 1000));
    }
}

std::string SuDaemon::resolvePackageName(uint32_t pid) {
    std::string path = "/proc/" + std::to_string(pid) + "/cmdline";
    auto content = readFile(path);
    if (!content) return "";
    // cmdline 用 \0 分隔，取第一个
    size_t nul = content->find('\0');
    return nul == std::string::npos ? *content : content->substr(0, nul);
}

bool SuDaemon::handleRequest(const SuRequest& req) {
    NX_LOG_I("SuDaemon", "request from uid=%u pid=%u pkg=%s cmd=%s",
             req.uid, req.pid, req.packageName.c_str(), req.command.c_str());

    // 1. 查询现有策略
    Policy policy;
    {
        std::lock_guard<std::mutex> lk(mu_);
        policy = lookupPolicy(req.uid, req.packageName);
    }

    // 2. 若无策略（Deny），通过 EventBus 推送 SuRequestEvent 给 Manager
    //    Manager 弹出 SuRequestActivity 让用户决定
    if (policy == Policy::Deny) {
        // 检查是否在 apps_ 中（区分"无策略"和"已拒绝"）
        bool hasApp = false;
        {
            std::lock_guard<std::mutex> lk(mu_);
            for (auto& app : apps_) {
                if (app.uid == req.uid && app.packageName == req.packageName) {
                    hasApp = true;
                    break;
                }
            }
        }
        if (!hasApp) {
            // 推送请求事件给 Manager
            // Manager 调用 am start SuRequestActivity 弹出对话框
            // 简化：直接默认拒绝（生产应等待 Manager 响应）
            NX_LOG_I("SuDaemon", "no policy for uid=%u, defaulting to deny (Manager should handle)",
                     req.uid);
            // TODO: 通过 globalBus().publishSuRequest 推送事件，
            //       并等待 IPC setSuPolicy 调用
            addLog(req, false);
            return false;
        }
        // 已有 Deny 策略
        addLog(req, false);
        return false;
    }

    // 3. 授权
    addLog(req, true);
    // 更新 request count
    {
        std::lock_guard<std::mutex> lk(mu_);
        for (auto& app : apps_) {
            if (app.uid == req.uid && app.packageName == req.packageName) {
                app.requestCount++;
                app.lastRequestMs = nowMs();
                break;
            }
        }
    }
    savePolicy();

    // 4. fork root shell
    forkRootShell(req);
    return true;
}

void SuDaemon::forkRootShell(const SuRequest& req) {
    NX_LOG_I("SuDaemon", "forking root shell for uid=%u", req.uid);

#ifdef __ANDROID__
    // 用 forkpty 创建 PTY
    int masterFd = -1;
    pid_t pid = forkpty(&masterFd, nullptr, nullptr, nullptr);
#else
    // host 测试：用 pipe 替代（不能真正 forkpty）
    int masterFd = -1;
    pid_t pid = ::fork();
#endif
    if (pid < 0) {
        NX_LOG_E("SuDaemon", "fork failed: %s", ::strerror(errno));
        return;
    }

    if (pid == 0) {
        // child: root shell
        // 设置 UID/GID 为 root
        ::setgid(0);
        ::setuid(0);
        // 设置环境变量
        ::setenv("HOME", "/root", 1);
        ::setenv("PATH", "/system/bin:/system/xbin:/sbin:/vendor/bin", 1);
        ::setenv("TERM", "xterm-256color", 1);
        // 设置 PS1
        ::setenv("PS1", "nexus_root # ", 1);
        // exec shell
        ::execl("/system/bin/sh", "sh", nullptr);
        // 若 exec 失败
        ::_exit(127);
    }

    // parent: 在 clientFd 与 masterFd 之间双向透传
    // 简化实现：用两个线程双向透传
    // 生产实现应使用 epoll 或 select
    auto forwardFn = [](int fromFd, int toFd) {
        char buf[4096];
        ssize_t n;
        while ((n = ::read(fromFd, buf, sizeof(buf))) > 0) {
            ssize_t w = 0;
            while (w < n) {
                ssize_t r = ::write(toFd, buf + w, n - w);
                if (r < 0) break;
                w += r;
            }
        }
        ::close(fromFd);
    };

    std::thread clientToMaster(forwardFn, req.clientFd, masterFd);
    std::thread masterToClient(forwardFn, masterFd, req.clientFd);

    // 等待 shell 退出
    int status = 0;
    ::waitpid(pid, &status, 0);
    NX_LOG_I("SuDaemon", "root shell exited (pid=%d status=%d)", pid, status);

    ::close(masterFd);
    clientToMaster.detach();
    masterToClient.detach();
}

Result<void> SuDaemon::startListening(const std::string& socketPath) {
    listenFd_ = ::socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (listenFd_ < 0) {
        return {unexpect, Err::IoError};
    }
    struct sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    if (socketPath.size() >= sizeof(addr.sun_path)) {
        ::close(listenFd_);
        listenFd_ = -1;
        return {unexpect, Err::InvalidArg};
    }
    ::snprintf(addr.sun_path, sizeof(addr.sun_path), "%s", socketPath.c_str());
    ::unlink(socketPath.c_str());
    if (::bind(listenFd_, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        ::close(listenFd_);
        listenFd_ = -1;
        return {unexpect, Err::IoError};
    }
    // su socket 需要 world-writable（任何 app 都可能调用 su）
    // 但通过 SO_PEERCRED 校验调用者
    ::chmod(socketPath.c_str(), 0666);
    if (::listen(listenFd_, 8) < 0) {
        ::close(listenFd_);
        listenFd_ = -1;
        return {unexpect, Err::IoError};
    }
    running_ = true;
    NX_LOG_I("SuDaemon", "listening on %s", socketPath.c_str());

    // accept 循环（实际应在单独线程）
    while (running_) {
        int cfd = ::accept4(listenFd_, nullptr, nullptr, SOCK_CLOEXEC);
        if (cfd < 0) {
            if (errno == EINTR) continue;
            break;
        }
        // 读取 SO_PEERCRED
        struct ucred uc;
        socklen_t len = sizeof(uc);
        if (::getsockopt(cfd, SOL_SOCKET, SO_PEERCRED, &uc, &len) < 0) {
            ::close(cfd);
            continue;
        }
        SuRequest req;
        req.uid = (uint32_t)uc.uid;
        req.pid = (uint32_t)uc.pid;
        req.packageName = resolvePackageName(req.pid);
        req.clientFd = cfd;
        // 读取命令（可选，由客户端通过协议发送）
        // MVP：留空，使用 shell 默认

        // 在新线程处理（避免阻塞 accept）
        std::thread([this, req]{ handleRequest(req); }).detach();
    }
    return {};
}

void SuDaemon::stopListening() {
    running_ = false;
    if (listenFd_ >= 0) {
        ::shutdown(listenFd_, SHUT_RDWR);
        ::close(listenFd_);
        listenFd_ = -1;
    }
}

int suClientMain(int argc, char** argv) {
    // su 客户端入口
    // 1. 连接到 /dev/socket/nexus_su.sock
    // 2. 发送 SuRequest（含 uid/pid/cmdline）
    // 3. 等待 daemon 响应
    // 4. 若授权，双向透传 stdin/stdout 与 daemon
    int fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) {
        std::fprintf(stderr, "su: cannot create socket\n");
        return 1;
    }
    struct sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    ::strncpy(addr.sun_path, "/dev/socket/nexus_su.sock", sizeof(addr.sun_path) - 1);
    if (::connect(fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        std::fprintf(stderr, "su: cannot connect to nexusd su daemon\n");
        ::close(fd);
        return 1;
    }

    // 简化：发送一个字节让 daemon 通过 SO_PEERCRED 获取 uid/pid
    char hello = 'S';
    ::write(fd, &hello, 1);

    // 等待响应
    char response = 0;
    if (::read(fd, &response, 1) != 1) {
        std::fprintf(stderr, "su: no response from daemon\n");
        ::close(fd);
        return 1;
    }

    if (response != 'G') {   // 'G' = Granted
        std::fprintf(stderr, "su: permission denied\n");
        ::close(fd);
        return 1;
    }

    // 授权成功，双向透传
    auto forwardFn = [fd](int fromFd) {
        char buf[4096];
        ssize_t n;
        while ((n = ::read(fromFd, buf, sizeof(buf))) > 0) {
            ::write(fd, buf, n);
        }
    };
    std::thread stdinThread(forwardFn, STDIN_FILENO);
    char buf[4096];
    ssize_t n;
    while ((n = ::read(fd, buf, sizeof(buf))) > 0) {
        ::write(STDOUT_FILENO, buf, n);
    }
    stdinThread.detach();
    ::close(fd);
    return 0;
}

} // namespace nexus
