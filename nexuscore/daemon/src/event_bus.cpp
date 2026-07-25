#include "nexus/event_bus.h"
#include "nexus/log.h"

#include <algorithm>
#include <chrono>
#include <utility>

namespace nexus {

EventBus& globalBus() {
    static EventBus instance;
    return instance;
}

size_t EventBus::subscribe(Subscriber sub) {
    std::lock_guard<std::mutex> lk(mu_);
    size_t id = nextId_++;
    subs_.emplace_back(id, std::move(sub));
    return id;
}

void EventBus::unsubscribe(size_t id) {
    std::lock_guard<std::mutex> lk(mu_);
    subs_.erase(std::remove_if(subs_.begin(), subs_.end(),
                               [id](const auto& p) { return p.first == id; }),
                subs_.end());
}

void EventBus::publish(Event ev) {
    // 复制订阅者列表，避免回调中再次订阅/取消订阅造成迭代器失效
    std::vector<std::pair<size_t, Subscriber>> snapshot;
    {
        std::lock_guard<std::mutex> lk(mu_);
        snapshot = subs_;
    }
    for (auto& [id, sub] : snapshot) {
        sub(ev);
    }
}

void EventBus::publishLog(log::Level level, const std::string& tag, const std::string& msg) {
    Event ev;
    ev.name = "LOG_LINE";
    ev.timestampMs = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    ev.fields["level"] = std::to_string((int)level);
    ev.fields["tag"] = tag;
    ev.fields["msg"] = msg;
    publish(std::move(ev));
}

void EventBus::publishModuleLoaded(const std::string& moduleId) {
    Event ev;
    ev.name = "MODULE_LOADED";
    ev.timestampMs = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    ev.fields["id"] = moduleId;
    publish(std::move(ev));
}

void EventBus::publishModuleUnloaded(const std::string& moduleId) {
    Event ev;
    ev.name = "MODULE_UNLOADED";
    ev.timestampMs = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    ev.fields["id"] = moduleId;
    publish(std::move(ev));
}

void EventBus::publishScriptDone(const std::string& script, int code) {
    Event ev;
    ev.name = "SCRIPT_DONE";
    ev.timestampMs = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    ev.fields["script"] = script;
    ev.fields["code"] = std::to_string(code);
    publish(std::move(ev));
}

void EventBus::publishDaemonReady() {
    Event ev;
    ev.name = "DAEMON_READY";
    ev.timestampMs = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    publish(std::move(ev));
    NX_LOG_I("EventBus", "DAEMON_READY published");
}

void EventBus::publishSuRequest(const std::string& pkg, uint32_t uid, uint32_t pid, const std::string& cmd) {
    Event ev;
    ev.name = "SU_REQUEST";
    ev.timestampMs = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    ev.fields["package_name"] = pkg;
    ev.fields["uid"] = std::to_string(uid);
    ev.fields["pid"] = std::to_string(pid);
    ev.fields["command"] = cmd;
    publish(std::move(ev));
}

} // namespace nexus
