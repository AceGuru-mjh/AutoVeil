#include "nexus/ipc/handlers.h"
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
            return makeResponse(-1, "unknown RPC");
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

std::vector<uint8_t> Handlers::handlePing(Context& ctx, Decoder& d) {
    std::string token = d.getStr();
    NX_LOG_D("Handlers", "ping: %s", token.c_str());
    Encoder enc;
    enc.putU32(0);   // code
    enc.putStr("ok");
    enc.putStr(token);
    enc.putU32(1);   // server_version
    enc.putEnd();
    return std::move(enc).bytes();
}

std::vector<uint8_t> Handlers::handleGetStatus(Context& ctx, Decoder& d) {
    // TODO: 从 ctx.core 取真实状态
    Encoder enc;
    enc.putU32(0);   // code
    enc.putStr("ok");
    enc.putBool(true);                    // root_available
    enc.putStr("magisk");                 // root_provider
    enc.putStr("30.0");                   // root_version
    enc.putBool(true);                    // selinux_enforcing
    enc.putStr("u:r:nexus_daemon:s0");    // selinux_domain
    enc.putBool(true);                    // daemon_running
    enc.putU32((uint32_t)::getpid());     // daemon_pid
    enc.putStr("overlayfs");              // fs_interceptor
    enc.putU32(0);                        // module_count (TODO)
    enc.putBool(false);                   // safe_mode
    enc.putU64(0);                        // uptime_ms (TODO)
    enc.putStr("14");                     // android_version
    enc.putStr("2024-01-01");             // security_patch
    enc.putStr("5.15.0");                 // kernel_version
    enc.putStr("arm64");                  // arch
    enc.putStr(NEXUS_VERSION);            // daemon_version
    enc.putEnd();
    return std::move(enc).bytes();
}

std::vector<uint8_t> Handlers::handleListModules(Context& ctx, Decoder& d) {
    Encoder enc;
    enc.putU32(0);
    enc.putStr("ok");
    // TODO: 真实模块列表
    enc.putU32(0);   // modules count = 0
    enc.putEnd();
    return std::move(enc).bytes();
}

std::vector<uint8_t> Handlers::handleEnableModule(Context& ctx, Decoder& d) {
    std::string id = d.getStr();
    NX_LOG_I("Handlers", "enable module: %s", id.c_str());
    return makeOk();
}

std::vector<uint8_t> Handlers::handleDisableModule(Context& ctx, Decoder& d) {
    std::string id = d.getStr();
    NX_LOG_I("Handlers", "disable module: %s", id.c_str());
    return makeOk();
}

std::vector<uint8_t> Handlers::handleInstallModule(Context& ctx, Decoder& d) {
    std::string path = d.getStr();
    NX_LOG_I("Handlers", "install module: %s", path.c_str());
    // TODO: 实际安装流程
    Encoder enc;
    enc.putU32(0);
    enc.putStr("ok");
    enc.putStr("dummy_id");
    enc.putBool(true);   // need_reboot
    enc.putEnd();
    return std::move(enc).bytes();
}

std::vector<uint8_t> Handlers::handleUninstallModule(Context& ctx, Decoder& d) {
    std::string id = d.getStr();
    NX_LOG_I("Handlers", "uninstall module: %s", id.c_str());
    return makeOk();
}

std::vector<uint8_t> Handlers::handleRestartDaemon(Context& ctx, Decoder& d) {
    NX_LOG_I("Handlers", "restart daemon requested");
    // TODO: 触发 daemon 自重启
    return makeOk();
}

std::vector<uint8_t> Handlers::handleEnterSafeMode(Context& ctx, Decoder& d) {
    uint32_t timeout = d.getU32();
    NX_LOG_I("Handlers", "enter safe mode, timeout=%u", timeout);
    return makeOk();
}

std::vector<uint8_t> Handlers::handleSubscribeLogs(Context& ctx, Decoder& d) {
    uint32_t minLevel = d.getU32();
    NX_LOG_I("Handlers", "subscribe logs, minLevel=%u", minLevel);
    // 实际订阅在 IpcServer 已通过 EventBus 自动处理
    return makeOk();
}

std::vector<uint8_t> Handlers::handleListSuApps(Context& ctx, Decoder& d) {
    Encoder enc;
    enc.putU32(0);
    enc.putStr("ok");
    enc.putU32(0);   // apps count = 0
    enc.putEnd();
    return std::move(enc).bytes();
}

std::vector<uint8_t> Handlers::handleSetSuPolicy(Context& ctx, Decoder& d) {
    std::string pkg = d.getStr();
    uint32_t uid = d.getU32();
    uint32_t policy = d.getU32();
    uint32_t timeout = d.getU32();
    NX_LOG_I("Handlers", "set su policy: pkg=%s uid=%u policy=%u timeout=%u",
             pkg.c_str(), uid, policy, timeout);
    return makeOk();
}

std::vector<uint8_t> Handlers::handleListSuLogs(Context& ctx, Decoder& d) {
    Encoder enc;
    enc.putU32(0);
    enc.putStr("ok");
    enc.putU32(0);
    enc.putEnd();
    return std::move(enc).bytes();
}

std::vector<uint8_t> Handlers::handleClearLogs(Context& ctx, Decoder& d) {
    uint32_t target = d.getU32();
    NX_LOG_I("Handlers", "clear logs, target=%u", target);
    return makeOk();
}

std::vector<uint8_t> Handlers::handleReboot(Context& ctx, Decoder& d) {
    uint32_t mode = d.getU32();
    NX_LOG_I("Handlers", "reboot mode=%u", mode);
    // 整改 #13：USERSPACE 模式需要底层 root 帮忙注入 sys.powerctl 写权限
    // 实际实现见 DaemonCore::reboot()
    return makeOk();
}

std::vector<uint8_t> Handlers::handleUninstallFramework(Context& ctx, Decoder& d) {
    NX_LOG_W("Handlers", "uninstall framework requested");
    // TODO: 实际卸载流程
    return makeOk();
}

} // namespace nexus::ipc
