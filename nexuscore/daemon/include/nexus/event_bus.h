#pragma once

#include "nexus/types.h"
#include "nexus/log.h"
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace nexus {

// 内存事件总线
//
// MVP 单进程内 pub/sub。Phase 3 会扩展为跨进程事件总线。
// 用途：
// - 模块加载/卸载事件
// - 脚本执行完成事件
// - SuRequest 事件（订阅者：Manager）
// - 日志事件（订阅者：Manager LogsPage）

struct Event {
    std::string name;        // 如 "LOG_LINE" / "MODULE_LOADED" / "SCRIPT_DONE" / "SU_REQUEST" / "DAEMON_READY"
    uint64_t timestampMs;
    // payload 以 string/数字形式携带（MVP 不引入 protobuf 依赖）
    std::unordered_map<std::string, std::string> fields;

    /// Phase 2: const 安全的字段查找，避免在 const Event& 上用 operator[] 触发插入
    /// @return 字段值指针，不存在返回 nullptr
    const std::string* findField(const std::string& key) const {
        auto it = fields.find(key);
        return it != fields.end() ? &it->second : nullptr;
    }

    /// 便捷获取字段值，不存在返回 default
    std::string getField(const std::string& key, const std::string& defaultVal = "") const {
        auto it = fields.find(key);
        return it != fields.end() ? it->second : defaultVal;
    }
};

class EventBus {
public:
    using Subscriber = std::function<void(const Event&)>;

    // 订阅，返回 subscriber id（用于取消）
    size_t subscribe(Subscriber sub);
    void unsubscribe(size_t id);

    // 发布（同步调用所有订阅者，注意避免递归发布）
    void publish(Event ev);

    // 便捷发布
    void publishLog(log::Level level, const std::string& tag, const std::string& msg);
    void publishModuleLoaded(const std::string& moduleId);
    void publishModuleUnloaded(const std::string& moduleId);
    void publishScriptDone(const std::string& script, int code);
    void publishDaemonReady();
    void publishSuRequest(const std::string& pkg, uint32_t uid, uint32_t pid, const std::string& cmd);

private:
    std::mutex mu_;
    std::vector<std::pair<size_t, Subscriber>> subs_;
    size_t nextId_ = 1;
};

// 全局单例（main.cpp 创建并注入到各组件）
EventBus& globalBus();

} // namespace nexus
