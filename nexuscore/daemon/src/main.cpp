// NexusCore Daemon (nexusd) 入口
//
// 流程：
//   1. 解析 argv，加载 config.toml（MVP 跳过，使用硬编码默认值）
//   2. 写 PID 文件 /dev/nexusd.pid
//   3. RootEnvironmentDetector::detect()
//      - 失败：进入只读模式（仅 IPC + 状态查询）
//      - 成功：继续 4-7
//   4. SELinuxManager::patchSelfDomain()（失败不致命）
//   5. DaemonCore::start() —— 扫描模块、挂载 system/、执行 post-fs-data 脚本
//   6. 启动 IPC server，监听 /dev/socket/nexusd.sock
//   7. 监听 sys.boot_completed=1，触发 late_start 脚本
//   8. 等待 SIGTERM/SIGINT，优雅退出
//
// 重要约束（来自 spec-01 §1.1）：
//   - 任何 syscall 失败必须有 fallback，绝不导致 Bootloop
//   - 客户端绝不直接执行 Root 命令（本进程是唯一 root 入口）

#include "nexus/log.h"
#include "nexus/util.h"
#include "nexus/env_detector.h"
#include "nexus/selinux_manager.h"
#include "nexus/daemon_core.h"
#include "nexus/ipc/ipc_server.h"
#include "nexus/capability/capability_manager.h"
#include "nexus/permission/provider.h"

#include <atomic>
#include <csignal>
#include <cstdlib>
#include <iostream>
#include <string>
#include <string_view>
#include <unistd.h>

static std::atomic<bool> g_shouldExit{false};

static void signalHandler(int sig) {
    g_shouldExit.store(true);
}

static void printUsage() {
    std::cerr <<
        "nexusd " NEXUS_VERSION "\n"
        "Usage: nexusd [options]\n"
        "Options:\n"
        "  --foreground       Run in foreground (log to stderr)\n"
        "  --socket PATH      Override socket path (default: /dev/socket/nexusd.sock)\n"
        "  --readonly         Force read-only mode (no mount, no scripts)\n"
        "  --version          Print version and exit\n"
        "  --help             Show this help\n";
}

int main(int argc, char** argv) {
    bool foreground = false;
    bool forceReadOnly = false;
    std::string socketPath = "/dev/socket/nexusd.sock";

    for (int i = 1; i < argc; ++i) {
        std::string_view arg = argv[i];
        if (arg == "--foreground") foreground = true;
        else if (arg == "--readonly") forceReadOnly = true;
        else if (arg == "--socket" && i + 1 < argc) socketPath = argv[++i];
        else if (arg == "--version") {
            std::cout << "nexusd " NEXUS_VERSION << "\n";
            return 0;
        } else if (arg == "--help" || arg == "-h") {
            printUsage();
            return 0;
        } else {
            std::cerr << "Unknown arg: " << arg << "\n";
            printUsage();
            return 1;
        }
    }

    // 初始化日志
    nexus::log::init("/data/adb/nexuscore/logs");
    if (foreground) {
        // 前台模式同时输出到 stderr（便于 adb shell 调试）
        nexus::log::setMinLevel(nexus::log::Level::Debug);
    }

    NX_LOG_I("main", "nexusd %s starting (pid=%d)", NEXUS_VERSION, (int)getpid());

    // 信号处理
    std::signal(SIGTERM, signalHandler);
    std::signal(SIGINT,  signalHandler);

    // ====== Phase 2: Capability 检测 ======
    // 先检测设备能力，生成能力矩阵，后续功能根据能力决定是否启用
    auto& capability = nexus::CapabilityManager::instance();
    capability.detect();
    NX_LOG_I("main", "%s", capability.report().c_str());

    // ====== Phase 3: Permission 初始化 ======
    // 当前阶段使用 NoRootProvider（用户态核心框架）
    // 未来切换到 NexusRootProvider / MagiskProvider / KsuProvider
    nexus::NoRootProvider permissionProvider;
    NX_LOG_I("main", "permission provider: %s (level=%d)",
             permissionProvider.name().c_str(),
             (int)permissionProvider.level());

    // PID 文件（防双实例）
    nexus::PidFile pidFile("/dev/nexusd.pid");
    if (auto r = pidFile.acquire(); !r) {
        NX_LOG_E("main", "pid file acquire failed: %s", nexus::errString(r.error()));
        return (int)r.error();
    }

    // Root 环境探测
    auto envR = nexus::RootEnvironmentDetector::detect();
    if (!envR) {
        NX_LOG_E("main", "root env detect failed (err=%s); enter read-only mode",
                 nexus::errString(envR.error()));
        // 整改 #7（原 bug）：原代码继续 *env 会 UB。正确做法：进入只读模式。
        auto core = std::make_unique<nexus::DaemonCore>(nexus::RootEnvironment{});
        if (auto r = core->startReadOnly(); !r) {
            NX_LOG_E("main", "startReadOnly failed: %s", nexus::errString(r.error()));
            return (int)r.error();
        }
        // 启动 IPC（只响应 ping / get_status，core 仍注入但 env 为空）
        nexus::ipc::IpcServer server(core->bus(), core.get());
        if (auto r = server.start(socketPath); !r) {
            NX_LOG_E("main", "IPC server start failed: %s", nexus::errString(r.error()));
            return (int)r.error();
        }
        NX_LOG_I("main", "nexusd running in READ-ONLY mode");
        while (!g_shouldExit.load()) sleep(1);
        server.stop();
        core->stop();
        return 0;
    }

    nexus::RootEnvironment env = std::move(*envR);
    NX_LOG_I("main", "root provider: %s %s",
             nexus::rootProviderName(env.provider), env.providerVersion.c_str());

    // SELinux 策略注入（失败不致命）
    nexus::SELinuxManager se(env);
    if (auto r = se.patchSelfDomain(); !r) {
        NX_LOG_W("main", "selinux patch failed (err=%s); continue in restricted mode",
                 nexus::errString(r.error()));
    }

    // 创建 DaemonCore 并启动
    auto core = std::make_unique<nexus::DaemonCore>(std::move(env));
    if (forceReadOnly) {
        if (auto r = core->startReadOnly(); !r) {
            NX_LOG_E("main", "startReadOnly failed: %s", nexus::errString(r.error()));
            return (int)r.error();
        }
    } else {
        if (auto r = core->start(); !r) {
            NX_LOG_E("main", "DaemonCore start failed: %s; fallback to read-only",
                     nexus::errString(r.error()));
            if (auto r2 = core->startReadOnly(); !r2) {
                NX_LOG_E("main", "startReadOnly also failed: %s", nexus::errString(r2.error()));
                return (int)r2.error();
            }
        }
    }

    // 启动 IPC 服务（Phase B：注入 core 让 handlers 调用真实方法）
    nexus::ipc::IpcServer server(core->bus(), core.get());
    if (auto r = server.start(socketPath); !r) {
        NX_LOG_E("main", "IPC server start failed: %s", nexus::errString(r.error()));
        return (int)r.error();
    }

    NX_LOG_I("main", "nexusd running, listening on %s", socketPath.c_str());
    core->bus().publishDaemonReady();

    // 主循环：等待退出信号
    while (!g_shouldExit.load()) {
        sleep(1);
    }

    NX_LOG_I("main", "nexusd shutting down");
    server.stop();
    core->stop();
    nexus::log::shutdown();
    return 0;
}
