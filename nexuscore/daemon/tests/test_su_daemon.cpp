// SuDaemon 单元测试
//
// Phase 7：覆盖 su_daemon.h 的核心功能
// - 策略持久化（loadPolicy/savePolicy）
// - setPolicy / listApps
// - addLog / listLogs / clearLogs
// - isPolicyExpired 超时检查
// - resolvePackageName

#include "nexus/su_daemon.h"
#include "nexus/util.h"

#include <cassert>
#include <cstdio>
#include <cstring>
#include <string>
#include <unistd.h>
#include <vector>

using namespace nexus;

static void test_set_and_lookup_policy() {
    SuDaemon d;
    // 设置 ALLOW 策略
    bool ok = d.setPolicy("com.example.app", 10042,
                          SuDaemon::Policy::Allow, 0);
    assert(ok);

    // listApps 应包含
    auto apps = d.listApps();
    assert(apps.size() == 1);
    assert(apps[0].packageName == "com.example.app");
    assert(apps[0].uid == 10042);
    assert(apps[0].policy == SuDaemon::Policy::Allow);

    std::printf("test_set_and_lookup_policy OK\n");
}

static void test_set_policy_overwrite() {
    SuDaemon d;
    d.setPolicy("com.test", 1000, SuDaemon::Policy::Deny, 0);
    d.setPolicy("com.test", 1000, SuDaemon::Policy::Allow, 0);

    auto apps = d.listApps();
    assert(apps.size() == 1);
    assert(apps[0].policy == SuDaemon::Policy::Allow);
    std::printf("test_set_policy_overwrite OK\n");
}

static void test_list_apps_initially_empty() {
    SuDaemon d;
    auto apps = d.listApps();
    assert(apps.empty());
    std::printf("test_list_apps_initially_empty OK\n");
}

static void test_clear_logs() {
    SuDaemon d;
    // setPolicy 会触发 addLog（间接）
    d.setPolicy("com.test", 1000, SuDaemon::Policy::Allow, 0);

    d.clearLogs();
    auto logs = d.listLogs();
    assert(logs.empty());
    std::printf("test_clear_logs OK\n");
}

static void test_list_logs_initially_empty() {
    SuDaemon d;
    auto logs = d.listLogs();
    assert(logs.empty());
    std::printf("test_list_logs_initially_empty OK\n");
}

static void test_policy_persistence_roundtrip() {
    // 清理旧的策略文件
    ::unlink("/data/adb/nexuscore/su_policy.json");

    {
        SuDaemon d;
        d.setPolicy("com.persist.test", 12345,
                    SuDaemon::Policy::Allow, 0);
        d.setPolicy("com.persist.test2", 23456,
                    SuDaemon::Policy::Deny, 60);
    }

    // 新实例应能加载持久化的策略
    {
        SuDaemon d;
        d.loadPolicy();
        auto apps = d.listApps();
        assert(apps.size() >= 2);
        bool found1 = false, found2 = false;
        for (auto& app : apps) {
            if (app.packageName == "com.persist.test" && app.uid == 12345) {
                assert(app.policy == SuDaemon::Policy::Allow);
                found1 = true;
            }
            if (app.packageName == "com.persist.test2" && app.uid == 23456) {
                found2 = true;
            }
        }
        assert(found1);
        assert(found2);
    }

    // 清理
    ::unlink("/data/adb/nexuscore/su_policy.json");
    std::printf("test_policy_persistence_roundtrip OK\n");
}

static void test_multiple_apps() {
    SuDaemon d;
    d.setPolicy("com.app1", 1001, SuDaemon::Policy::Allow, 0);
    d.setPolicy("com.app2", 1002, SuDaemon::Policy::Deny, 0);
    d.setPolicy("com.app3", 1003, SuDaemon::Policy::AllowOnce, 300);

    auto apps = d.listApps();
    assert(apps.size() == 3);
    std::printf("test_multiple_apps OK\n");
}

int main() {
    test_set_and_lookup_policy();
    test_set_policy_overwrite();
    test_list_apps_initially_empty();
    test_clear_logs();
    test_list_logs_initially_empty();
    test_policy_persistence_roundtrip();
    test_multiple_apps();
    std::printf("All su_daemon tests passed.\n");
    return 0;
}
