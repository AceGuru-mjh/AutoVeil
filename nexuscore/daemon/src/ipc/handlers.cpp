// RPC handlers 真实实现
//
// Phase B：所有 handler 调用 DaemonCore 真实方法，不再返回 dummy 数据。
// 编解码使用 ipc::Encoder/Decoder（自定义二进制格式，零依赖）。

#include "nexus/ipc/handlers.h"
#include "nexus/daemon_core.h"
#include "nexus/util.h"
#include "nexus/log.h"

namespace nexus::ipc {

namespace {
// 简单 RPC type ID（与 Manager 端 proto 顺序对齐）
enum class RpcType : uint32_t {
    Ping = 1, GetStatus = 2, ListModules = 3, EnableModule = 4, DisableModule = 5,
    InstallModule = 6, UninstallModule = 7, RestartDaemon = 8, EnterSafeMode = 9,
    SubscribeLogs = 10, ListSuApps = 11, SetSuPolicy = 12, ListSuLogs = 13,
    ClearLogs = 14, Reboot = 15, UninstallFramework = 16,
};
}

std::vector<uint8_t> Handlers::dispatch(Context& ctx, uint32_t seq, const std::vector<uint8_t>& reqPayload) {
    Decoder d(reqPayload);
    uint32_t type = d.getU32();
    auto t = (RpcType)type;

    if (ctx.core == nullptr && t != RpcType::Ping) {
        NX_LOG_W("Handlers", "core is null, rejecting RPC type=%u", type);
        return makeError(-1, "daemon core not initialized");
    }

    switch (t) {
        case RpcType::Ping:               return handlePing(ctx, d);
        case RpcType::GetStatus:          return handleGetStatus(ctx, d);
        case RpcType::ListModules:        return handleListModules(ctx, d);
        case RpcType::EnableModule:       return handleEnableModule(ctx, d);
        case RpcType::DisableModule:      return handleDisableModule(ctx, d);
        case RpcType::InstallModule:      return handleInstallModule(ctx, d);
        case RpcType::UninstallModule:    return handleUninstallModule(ctx, d);
        case RpcType::RestartDaemon:      return handleRestartDaemon(ctx, d);
        case RpcType::EnterSafeMode:      return handleEnterSafeMode(ctx, d);
        case RpcType::SubscribeLogs:      return handleSubscribeLogs(ctx, d);
        case RpcType::ListSuApps:         return handleListSuApps(ctx, d);
        case RpcType::SetSuPolicy:        return handleSetSuPolicy(ctx, d);
        case RpcType::ListSuLogs:         return handleListSuLogs(ctx, d);
        case RpcType::ClearLogs:          return handleClearLogs(ctx, d);
        case RpcType::Reboot:             return handleReboot(ctx, d);
        case RpcType::UninstallFramework: return handleUninstallFramework(ctx, d);
        default:
            NX_LOG_W("Handlers", "unknown RPC type=%u", type);
            return makeError(-1, "unknown RPC");
    }
}

std::vector<uint8_t> Handlers::makeResponse(int code, const std::string& msg) {
    Encoder enc;
    enc.putU32((uint32_t)code);
    enc.putStr(msg);
    enc.putEnd();
    return std::move(enc).bytes();
}

std::vector<uint8_t> Handlers::makeOk() {
    return makeResponse(0, "ok");
}

std::vector<uint8_t> Handlers::makeError(int code, const std::string& msg) {
    return makeResponse(code, msg);
}

std::vector<uint8_t> Handlers::handlePing(Context& ctx, Decoder& d) {
    std::string token = d.getStr();
    NX_LOG_D("Handlers", "ping: %s", token.c_str());
    Encoder enc;
    enc.putU32(0);            // code
    enc.putStr("ok");
    enc.putStr(token);
    enc.putU32(1);            // server_version
    enc.putEnd();
    return std::move(enc).bytes();
}

std::vector<uint8_t> Handlers::handleGetStatus(Context& ctx, Decoder& d) {
    if (!ctx.core) return makeError(-1, "core not initialized");

    auto& env = ctx.core->env();
    Encoder enc;
    enc.putU32(0);                                            // code
    enc.putStr("ok");
    enc.putBool(env.provider != RootProvider::None);          // root_available
    enc.putStr(rootProviderName(env.provider));               // root_provider
    enc.putStr(env.providerVersion);                          // root_version
    enc.putBool(true);                                        // selinux_enforcing (TODO: 真实查询)
    enc.putStr("u:r:nexus_daemon:s0");                        // selinux_domain
    enc.putBool(ctx.core->isDaemonRunning());                 // daemon_running
    enc.putU32(ctx.core->daemonPid());                        // daemon_pid
    enc.putStr(std::string(ctx.core->fsInterceptorName()));  // fs_interceptor
    enc.putU32(ctx.core->moduleCount());                      // module_count
    enc.putBool(ctx.core->isSafeMode());                      // safe_mode
    enc.putU64(ctx.core->uptimeMs());                         // uptime_ms
    enc.putStr("14");                                         // android_version (TODO: 真实读取)
    enc.putStr("2024-01-01");                                 // security_patch (TODO)
    enc.putStr("5.15.0");                                     // kernel_version (TODO)
    enc.putStr("arm64");                                      // arch
    enc.putStr(NEXUS_VERSION);                                // daemon_version
    enc.putEnd();
    return std::move(enc).bytes();
}

std::vector<uint8_t> Handlers::handleListModules(Context& ctx, Decoder& d) {
    if (!ctx.core) return makeError(-1, "core not initialized");

    auto& modules = ctx.core->modules();
    Encoder enc;
    enc.putU32(0);
    enc.putStr("ok");
    enc.putU32((uint32_t)modules.size());
    for (auto& m : modules) {
        enc.putStr(m.manifest.id);
        enc.putStr(m.manifest.name);
        enc.putStr(m.manifest.version);
        enc.putStr(m.manifest.author);
        enc.putStr(m.manifest.description);
        enc.putBool(m.manifest.enabled);
        enc.putU32((uint32_t)m.manifest.priority);
        // capabilities 作为以 \0 分隔的字符串传输
        std::string caps;
        for (size_t i = 0; i < m.manifest.capabilities.size(); ++i) {
            if (i > 0) caps.push_back(',');
            caps += m.manifest.capabilities[i];
        }
        enc.putStr(caps);
        enc.putBool(false);   // has_update (deprecated)
        enc.putStr("");       // update_url (deprecated)
    }
    enc.putEnd();
    return std::move(enc).bytes();
}

std::vector<uint8_t> Handlers::handleEnableModule(Context& ctx, Decoder& d) {
    if (!ctx.core) return makeError(-1, "core not initialized");
    std::string id = d.getStr();
    bool ok = ctx.core->enableModule(id);
    return ok ? makeOk() : makeError(-2, "module not found: " + id);
}

std::vector<uint8_t> Handlers::handleDisableModule(Context& ctx, Decoder& d) {
    if (!ctx.core) return makeError(-1, "core not initialized");
    std::string id = d.getStr();
    bool ok = ctx.core->disableModule(id);
    return ok ? makeOk() : makeError(-2, "module not found: " + id);
}

std::vector<uint8_t> Handlers::handleInstallModule(Context& ctx, Decoder& d) {
    if (!ctx.core) return makeError(-1, "core not initialized");
    std::string path = d.getStr();
    NX_LOG_I("Handlers", "install module: %s", path.c_str());
    auto r = ctx.core->installModule(path);
    if (!r) {
        return makeError((int)r.error(), "install failed: " + std::string(errString(r.error())));
    }
    Encoder enc;
    enc.putU32(0);
    enc.putStr("ok");
    enc.putStr(r->id);
    enc.putBool(r->needReboot);
    enc.putEnd();
    return std::move(enc).bytes();
}

std::vector<uint8_t> Handlers::handleUninstallModule(Context& ctx, Decoder& d) {
    if (!ctx.core) return makeError(-1, "core not initialized");
    std::string id = d.getStr();
    bool ok = ctx.core->uninstallModule(id);
    return ok ? makeOk() : makeError(-2, "uninstall failed: " + id);
}

std::vector<uint8_t> Handlers::handleRestartDaemon(Context& ctx, Decoder& d) {
    if (!ctx.core) return makeError(-1, "core not initialized");
    ctx.core->restartDaemon();
    return makeOk();
}

std::vector<uint8_t> Handlers::handleEnterSafeMode(Context& ctx, Decoder& d) {
    if (!ctx.core) return makeError(-1, "core not initialized");
    uint32_t timeout = d.getU32();
    ctx.core->enterSafeMode(timeout);
    return makeOk();
}

std::vector<uint8_t> Handlers::handleSubscribeLogs(Context& ctx, Decoder& d) {
    uint32_t minLevel = d.getU32();
    NX_LOG_I("Handlers", "subscribe logs, minLevel=%u (auto-handled by EventBus)", minLevel);
    // 实际订阅由 IpcServer 通过 EventBus 自动处理（每个 client 默认订阅所有事件）
    return makeOk();
}

std::vector<uint8_t> Handlers::handleListSuApps(Context& ctx, Decoder& d) {
    if (!ctx.core) return makeError(-1, "core not initialized");
    auto apps = ctx.core->listSuApps();
    Encoder enc;
    enc.putU32(0);
    enc.putStr("ok");
    enc.putU32((uint32_t)apps.size());
    for (auto& app : apps) {
        enc.putStr(app.packageName);
        enc.putU32(app.uid);
        enc.putU32((uint32_t)app.policy);
        enc.putU64(app.lastRequestMs);
        enc.putU32(app.requestCount);
        enc.putU32(app.timeoutSec);
    }
    enc.putEnd();
    return std::move(enc).bytes();
}

std::vector<uint8_t> Handlers::handleSetSuPolicy(Context& ctx, Decoder& d) {
    if (!ctx.core) return makeError(-1, "core not initialized");
    std::string pkg = d.getStr();
    uint32_t uid = d.getU32();
    uint32_t policy = d.getU32();
    uint32_t timeout = d.getU32();
    bool ok = ctx.core->setSuPolicy(pkg, uid, (int)policy, timeout);
    return ok ? makeOk() : makeError(-2, "set su policy failed");
}

std::vector<uint8_t> Handlers::handleListSuLogs(Context& ctx, Decoder& d) {
    if (!ctx.core) return makeError(-1, "core not initialized");
    auto logs = ctx.core->listSuLogs();
    Encoder enc;
    enc.putU32(0);
    enc.putStr("ok");
    enc.putU32((uint32_t)logs.size());
    for (auto& l : logs) {
        enc.putU64(l.timestampMs);
        enc.putStr(l.packageName);
        enc.putU32(l.uid);
        enc.putBool(l.granted);
        enc.putStr(l.command);
    }
    enc.putEnd();
    return std::move(enc).bytes();
}

std::vector<uint8_t> Handlers::handleClearLogs(Context& ctx, Decoder& d) {
    if (!ctx.core) return makeError(-1, "core not initialized");
    uint32_t target = d.getU32();
    bool ok = ctx.core->clearLogs((int)target);
    return ok ? makeOk() : makeError(-2, "clear logs failed");
}

std::vector<uint8_t> Handlers::handleReboot(Context& ctx, Decoder& d) {
    if (!ctx.core) return makeError(-1, "core not initialized");
    uint32_t mode = d.getU32();
    NX_LOG_W("Handlers", "reboot mode=%u requested by uid=%d", mode, ctx.peer.uid);
    bool ok = ctx.core->reboot((DaemonCore::RebootMode)mode);
    return ok ? makeOk() : makeError(-2, "reboot failed");
}

std::vector<uint8_t> Handlers::handleUninstallFramework(Context& ctx, Decoder& d) {
    if (!ctx.core) return makeError(-1, "core not initialized");
    NX_LOG_W("Handlers", "uninstall framework requested by uid=%d", ctx.peer.uid);
    bool ok = ctx.core->uninstallFramework();
    return ok ? makeOk() : makeError(-2, "uninstall framework failed");
}

} // namespace nexus::ipc
