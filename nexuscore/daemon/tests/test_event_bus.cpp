// 测试：EventBus pub/sub

#include "nexus/event_bus.h"

#include <atomic>
#include <cassert>
#include <cstdio>
#include <thread>
#include <vector>

using namespace nexus;

static void test_basic_pub_sub() {
    EventBus bus;
    int received = 0;
    auto id = bus.subscribe([&](const Event& ev) {
        if (ev.name == "TEST_EVENT") ++received;
    });
    bus.publish(Event{"TEST_EVENT", 0, {}});
    bus.publish(Event{"OTHER_EVENT", 0, {}});
    bus.publish(Event{"TEST_EVENT", 0, {}});
    assert(received == 2);
    bus.unsubscribe(id);
    std::printf("test_basic_pub_sub OK\n");
}

static void test_unsubscribe() {
    EventBus bus;
    int received = 0;
    auto id = bus.subscribe([&](const Event& ev) {
        ++received;
    });
    bus.publish(Event{"A", 0, {}});
    bus.unsubscribe(id);
    bus.publish(Event{"B", 0, {}});
    bus.publish(Event{"C", 0, {}});
    assert(received == 1);
    std::printf("test_unsubscribe OK\n");
}

static void test_multiple_subscribers() {
    EventBus bus;
    int a = 0, b = 0;
    auto id1 = bus.subscribe([&](const Event&) { ++a; });
    auto id2 = bus.subscribe([&](const Event&) { ++b; });
    bus.publish(Event{"X", 0, {}});
    assert(a == 1);
    assert(b == 1);
    bus.unsubscribe(id1);
    bus.publish(Event{"Y", 0, {}});
    assert(a == 1);
    assert(b == 2);
    bus.unsubscribe(id2);
    std::printf("test_multiple_subscribers OK\n");
}

static void test_subscribe_during_publish() {
    // 订阅者回调中再订阅不应导致迭代器失效
    EventBus bus;
    int count = 0;
    auto id1 = bus.subscribe([&](const Event& ev) {
        ++count;
        if (count == 1) {
            bus.subscribe([&](const Event&) { ++count; });
        }
    });
    bus.publish(Event{"A", 0, {}});
    // 第一次 publish：id1 触发 count=1，新增订阅者（不触发本次）
    assert(count == 1);
    bus.publish(Event{"B", 0, {}});
    // 第二次 publish：id1 + 新订阅者都触发
    assert(count == 3);
    std::printf("test_subscribe_during_publish OK\n");
}

static void test_event_fields() {
    EventBus bus;
    std::string capturedName;
    std::string capturedField;
    bus.subscribe([&](const Event& ev) {
        capturedName = ev.name;
        auto it = ev.fields.find("key");
        if (it != ev.fields.end()) capturedField = it->second;
    });
    Event ev;
    ev.name = "FIELD_TEST";
    ev.fields["key"] = "value123";
    bus.publish(std::move(ev));
    assert(capturedName == "FIELD_TEST");
    assert(capturedField == "value123");
    std::printf("test_event_fields OK\n");
}

static void test_publish_log_helper() {
    EventBus bus;
    log::Level capturedLevel = log::Level::Info;
    std::string capturedTag, capturedMsg;
    bus.subscribe([&](const Event& ev) {
        if (ev.name == "LOG_LINE") {
            capturedLevel = (log::Level)std::stoul(ev.fields["level"]);
            capturedTag = ev.fields["tag"];
            capturedMsg = ev.fields["msg"];
        }
    });
    bus.publishLog(log::Level::Warn, "TestTag", "warning message");
    assert(capturedLevel == log::Level::Warn);
    assert(capturedTag == "TestTag");
    assert(capturedMsg == "warning message");
    std::printf("test_publish_log_helper OK\n");
}

static void test_publish_module_loaded() {
    EventBus bus;
    std::string capturedId;
    bus.subscribe([&](const Event& ev) {
        if (ev.name == "MODULE_LOADED") {
            capturedId = ev.fields["id"];
        }
    });
    bus.publishModuleLoaded("test_module_123");
    assert(capturedId == "test_module_123");
    std::printf("test_publish_module_loaded OK\n");
}

static void test_publish_su_request() {
    EventBus bus;
    std::string pkg, cmd;
    uint32_t uid = 0, pid = 0;
    bus.subscribe([&](const Event& ev) {
        if (ev.name == "SU_REQUEST") {
            pkg = ev.fields["package_name"];
            uid = std::stoul(ev.fields["uid"]);
            pid = std::stoul(ev.fields["pid"]);
            cmd = ev.fields["command"];
        }
    });
    bus.publishSuRequest("com.example.app", 10042, 12345, "su -c id");
    assert(pkg == "com.example.app");
    assert(uid == 10042);
    assert(pid == 12345);
    assert(cmd == "su -c id");
    std::printf("test_publish_su_request OK\n");
}

static void test_thread_safety() {
    // 多线程同时 publish 与 subscribe
    EventBus bus;
    std::atomic<int> total{0};
    auto id = bus.subscribe([&](const Event&) { total.fetch_add(1); });

    std::vector<std::thread> threads;
    for (int t = 0; t < 4; ++t) {
        threads.emplace_back([&bus]() {
            for (int i = 0; i < 100; ++i) {
                bus.publish(Event{"T", 0, {}});
            }
        });
    }
    for (auto& th : threads) th.join();
    assert(total.load() == 400);
    bus.unsubscribe(id);
    std::printf("test_thread_safety OK (400 events delivered across 4 threads)\n");
}

int main() {
    test_basic_pub_sub();
    test_unsubscribe();
    test_multiple_subscribers();
    test_subscribe_during_publish();
    test_event_fields();
    test_publish_log_helper();
    test_publish_module_loaded();
    test_publish_su_request();
    test_thread_safety();
    std::printf("All event_bus tests passed.\n");
    return 0;
}
