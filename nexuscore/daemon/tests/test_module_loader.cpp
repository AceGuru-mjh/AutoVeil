// 测试：ModuleLoader manifest 解析与 capabilities 校验

#include "nexus/module_loader.h"
#include "nexus/util.h"

#include <cassert>
#include <cstdio>
#include <string>

using namespace nexus;

static RootEnvironment makeTestEnv() {
    RootEnvironment env;
    env.provider = RootProvider::Magisk;
    env.modulesDir = "/tmp/nexus_test_modules";
    env.overlayBase = "/tmp/nexus_test_overlay";
    mkdirRecursive(env.modulesDir, 0755);
    mkdirRecursive(env.overlayBase, 0755);
    return env;
}

static void cleanupModules() {
    execCommand("rm -rf /tmp/nexus_test_modules", 5);
    execCommand("rm -rf /tmp/nexus_test_overlay", 5);
}

static bool writeManifest(const std::string& id, const std::string& content) {
    std::string dir = "/tmp/nexus_test_modules/" + id;
    mkdirRecursive(dir, 0755);
    return writeFile(dir + "/manifest.json", content, 0644);
}

static void test_parse_valid_manifest() {
    cleanupModules();
    writeManifest("test_mod_1", R"({
        "id": "test_mod_1",
        "name": "Test Module",
        "version": "1.0.0",
        "versionCode": 1,
        "author": "tester",
        "description": "a test module",
        "min_nexus_version": "1.0",
        "priority": 100,
        "enabled": true,
        "capabilities": ["EXECUTE_SHELL", "MOUNT_FILESYSTEM"],
        "intents": [{"action": "SYSTEM_BOOT_COMPLETED", "priority": 10}],
        "homepage": "https://example.com"
    })");

    auto env = makeTestEnv();
    ModuleLoader loader(env, env.modulesDir);
    auto result = loader.scanModules();
    assert(result.has_value());
    assert(result->size() == 1);
    auto& m = result->at(0);
    assert(m.manifest.id == "test_mod_1");
    assert(m.manifest.name == "Test Module");
    assert(m.manifest.version == "1.0.0");
    assert(m.manifest.versionCode == 1);
    assert(m.manifest.author == "tester");
    assert(m.manifest.priority == 100);
    assert(m.manifest.enabled == true);
    assert(m.manifest.capabilities.size() == 2);
    assert(m.manifest.capabilities[0] == "EXECUTE_SHELL");
    assert(m.manifest.capabilities[1] == "MOUNT_FILESYSTEM");
    std::printf("test_parse_valid_manifest OK\n");
}

static void test_parse_invalid_id() {
    cleanupModules();
    // ID 大写开头不合法
    writeManifest("InvalidID", R"({
        "id": "InvalidID",
        "name": "Bad",
        "version": "1.0.0",
        "author": "test"
    })");

    auto env = makeTestEnv();
    ModuleLoader loader(env, env.modulesDir);
    auto result = loader.scanModules();
    assert(result.has_value());
    assert(result->size() == 0);   // 非法 ID 应被跳过
    std::printf("test_parse_invalid_id OK (rejected uppercase ID)\n");
}

static void test_parse_missing_required() {
    cleanupModules();
    // 缺 author
    writeManifest("missing_author", R"({
        "id": "missing_author",
        "name": "Bad",
        "version": "1.0.0"
    })");

    auto env = makeTestEnv();
    ModuleLoader loader(env, env.modulesDir);
    auto result = loader.scanModules();
    assert(result.has_value());
    assert(result->size() == 0);
    std::printf("test_parse_missing_required OK (rejected missing author)\n");
}

static void test_parse_malformed_json() {
    cleanupModules();
    writeManifest("bad_json", R"({
        "id": "bad_json",
        "name": "Bad",
        "version": "1.0.0",
        "author": "test",
        // 注释不合法
        invalid syntax
    })");

    auto env = makeTestEnv();
    ModuleLoader loader(env, env.modulesDir);
    auto result = loader.scanModules();
    assert(result.has_value());
    assert(result->size() == 0);
    std::printf("test_parse_malformed_json OK (rejected malformed JSON)\n");
}

static void test_priority_sorting() {
    cleanupModules();
    // 3 个模块，priority 分别 50, 200, 0
    writeManifest("mod_a", R"({"id":"mod_a","name":"A","version":"1.0","author":"t","priority":50})");
    writeManifest("mod_b", R"({"id":"mod_b","name":"B","version":"1.0","author":"t","priority":200})");
    writeManifest("mod_c", R"({"id":"mod_c","name":"C","version":"1.0","author":"t","priority":0})");

    auto env = makeTestEnv();
    ModuleLoader loader(env, env.modulesDir);
    auto result = loader.scanModules();
    assert(result.has_value());
    assert(result->size() == 3);
    // 按 priority 升序：c(0), a(50), b(200)
    assert(result->at(0).manifest.id == "mod_c");
    assert(result->at(1).manifest.id == "mod_a");
    assert(result->at(2).manifest.id == "mod_b");
    std::printf("test_priority_sorting OK (sorted by priority asc)\n");
}

static void test_capabilities_validation_warning() {
    cleanupModules();
    // 提供脚本但未声明 EXECUTE_SHELL → 应生成 warning
    writeManifest("no_shell_cap", R"({
        "id": "no_shell_cap",
        "name": "No Shell",
        "version": "1.0",
        "author": "t",
        "capabilities": []
    })");
    // 创建 service.sh
    mkdirRecursive("/tmp/nexus_test_modules/no_shell_cap", 0755);
    writeFile("/tmp/nexus_test_modules/no_shell_cap/service.sh",
              "#!/system/bin/sh\necho hi\n", 0755);

    auto env = makeTestEnv();
    ModuleLoader loader(env, env.modulesDir);
    auto result = loader.scanModules();
    assert(result.has_value());
    assert(result->size() == 1);
    auto& m = result->at(0);
    assert(m.hasService);
    auto report = loader.validate(m);
    assert(!report.warnings.empty());
    bool foundWarning = false;
    for (auto& w : report.warnings) {
        if (w.find("EXECUTE_SHELL") != std::string::npos) {
            foundWarning = true;
            break;
        }
    }
    assert(foundWarning);
    std::printf("test_capabilities_validation_warning OK\n");
}

static void test_collect_mount_targets() {
    cleanupModules();
    writeManifest("mount_mod", R"({
        "id": "mount_mod",
        "name": "Mount",
        "version": "1.0",
        "author": "t",
        "capabilities": ["MOUNT_FILESYSTEM"]
    })");
    // 创建 system/etc/hosts
    std::string modDir = "/tmp/nexus_test_modules/mount_mod";
    mkdirRecursive(modDir + "/system/etc", 0755);
    writeFile(modDir + "/system/etc/hosts", "127.0.0.1 localhost\n", 0644);
    writeFile(modDir + "/system/build.prop", "ro.test=1\n", 0644);

    auto env = makeTestEnv();
    ModuleLoader loader(env, env.modulesDir);
    auto result = loader.scanModules();
    assert(result.has_value());
    assert(result->size() == 1);
    auto& m = result->at(0);
    assert(m.systemFiles.size() == 2);
    auto targets = loader.collectMountTargets(m);
    assert(targets.size() == 2);
    // 验证 source / target 路径
    for (auto& t : targets) {
        assert(t.moduleId == "mount_mod");
        assert(t.target[0] == '/');   // 绝对路径
        assert(!t.source.empty());
    }
    std::printf("test_collect_mount_targets OK (found %zu targets)\n", targets.size());
}

static void test_no_mount_filesystem_capability() {
    cleanupModules();
    // 提供 system/ 文件但未声明 MOUNT_FILESYSTEM → collectMountTargets 返回空
    writeManifest("no_mount_cap", R"({
        "id": "no_mount_cap",
        "name": "NoMount",
        "version": "1.0",
        "author": "t",
        "capabilities": []
    })");
    std::string modDir = "/tmp/nexus_test_modules/no_mount_cap";
    mkdirRecursive(modDir + "/system/etc", 0755);
    writeFile(modDir + "/system/etc/hosts", "127.0.0.1 localhost\n", 0644);

    auto env = makeTestEnv();
    ModuleLoader loader(env, env.modulesDir);
    auto result = loader.scanModules();
    assert(result.has_value());
    assert(result->size() == 1);
    auto& m = result->at(0);
    assert(m.systemFiles.size() == 1);
    auto targets = loader.collectMountTargets(m);
    assert(targets.empty());   // 应为空，因为未声明 MOUNT_FILESYSTEM
    std::printf("test_no_mount_filesystem_capability OK (skipped mount targets)\n");
}

static void test_empty_modules_dir() {
    cleanupModules();
    auto env = makeTestEnv();
    ModuleLoader loader(env, env.modulesDir);
    auto result = loader.scanModules();
    assert(result.has_value());
    assert(result->empty());
    std::printf("test_empty_modules_dir OK\n");
}

static void test_nonexistent_modules_dir() {
    cleanupModules();
    auto env = makeTestEnv();
    env.modulesDir = "/tmp/nexus_test_modules_nonexistent";
    ModuleLoader loader(env, env.modulesDir);
    auto result = loader.scanModules();
    assert(result.has_value());       // 不视为错误
    assert(result->empty());
    std::printf("test_nonexistent_modules_dir OK\n");
}

int main() {
    test_parse_valid_manifest();
    test_parse_invalid_id();
    test_parse_missing_required();
    test_parse_malformed_json();
    test_priority_sorting();
    test_capabilities_validation_warning();
    test_collect_mount_targets();
    test_no_mount_filesystem_capability();
    test_empty_modules_dir();
    test_nonexistent_modules_dir();
    cleanupModules();
    std::printf("All module_loader tests passed.\n");
    return 0;
}
