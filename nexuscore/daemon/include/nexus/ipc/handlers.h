#pragma once

#include "nexus/types.h"
#include "nexus/event_bus.h"
#include "nexus/ipc/codec.h"
#include "nexus/ipc/credential_check.h"
#include "nexus/module_loader.h"
#include <map>
#include <memory>
#include <string>

namespace nexus {

// 前向声明
class DaemonCore;

namespace ipc {

// RPC 请求处理器
//
// 根据 Envelope.request.payload case 分发到对应 handler。
// Phase B：所有 handler 真实调用 DaemonCore 方法，不再返回 dummy 数据。
class Handlers {
public:
    struct Context {
        PeerCredential peer;
        EventBus& bus;
        DaemonCore* core;   // 不再为 nullptr，由 IpcServer 注入
    };

    // 处理一条已解码的 Request，返回 Response payload
    std::vector<uint8_t> dispatch(Context& ctx, uint32_t seq, const std::vector<uint8_t>& reqPayload);

private:
    // 各 RPC 的具体处理（参考 nexus.proto）
    std::vector<uint8_t> handlePing(Context& ctx, Decoder& d);
    std::vector<uint8_t> handleGetStatus(Context& ctx, Decoder& d);
    std::vector<uint8_t> handleListModules(Context& ctx, Decoder& d);
    std::vector<uint8_t> handleEnableModule(Context& ctx, Decoder& d);
    std::vector<uint8_t> handleDisableModule(Context& ctx, Decoder& d);
    std::vector<uint8_t> handleInstallModule(Context& ctx, Decoder& d);
    std::vector<uint8_t> handleUninstallModule(Context& ctx, Decoder& d);
    std::vector<uint8_t> handleRestartDaemon(Context& ctx, Decoder& d);
    std::vector<uint8_t> handleEnterSafeMode(Context& ctx, Decoder& d);
    std::vector<uint8_t> handleSubscribeLogs(Context& ctx, Decoder& d);
    std::vector<uint8_t> handleListSuApps(Context& ctx, Decoder& d);
    std::vector<uint8_t> handleSetSuPolicy(Context& ctx, Decoder& d);
    std::vector<uint8_t> handleListSuLogs(Context& ctx, Decoder& d);
    std::vector<uint8_t> handleClearLogs(Context& ctx, Decoder& d);
    std::vector<uint8_t> handleReboot(Context& ctx, Decoder& d);
    std::vector<uint8_t> handleUninstallFramework(Context& ctx, Decoder& d);

    // 构造 success / error Response
    std::vector<uint8_t> makeResponse(int code, const std::string& msg);
    std::vector<uint8_t> makeOk();
    std::vector<uint8_t> makeError(int code, const std::string& msg);
};

} // namespace nexus::ipc
} // namespace nexus
