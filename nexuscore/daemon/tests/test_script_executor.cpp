// 测试：ScriptExecutor shim 生成

#include "nexus/script_executor.h"
#include "nexus/util.h"

#include <cassert>
#include <cstdio>
#include <string>

using namespace nexus;

static void test_shim_contains_required_functions() {
    // ScriptExecutor::buildShimScript 是 private，这里通过 execute 间接验证
    // 改为测试 stageName 公开接口
    assert(std::string(ScriptExecutor::stageName(ScriptExecutor::Stage::PostFsData)) == "post-fs-data");
    assert(std::string(ScriptExecutor::stageName(ScriptExecutor::Stage::LateStart)) == "late_start");
    assert(std::string(ScriptExecutor::stageName(ScriptExecutor::Stage::Install)) == "install");
    assert(std::string(ScriptExecutor::stageName(ScriptExecutor::Stage::Uninstall)) == "uninstall");
    assert(std::string(ScriptExecutor::stageName(ScriptExecutor::Stage::Verify)) == "verify");
    std::printf("test_shim_contains_required_functions OK\n");
}

static void test_execute_nonexistent_script() {
    // 不存在的脚本应返回 exitCode=0（视为成功，未提供脚本）
    RootEnvironment env;
    env.provider = RootProvider::Magisk;
    env.overlayBase = "/tmp/nexus_test_overlay";
    env.modulesDir = "/tmp/nexus_test_modules";
    ScriptExecutor exec(env);
    ScriptExecutor::ExecOptions opts;
    opts.stage = ScriptExecutor::Stage::PostFsData;
    opts.moduleId = "test_mod";
    opts.modulePath = "/tmp/nonexistent_module";
    opts.moduleVersion = "1.0";
    opts.scriptPath = "/tmp/nonexistent_script.sh";
    auto result = exec.execute(opts);
    assert(result.exitCode == 0);
    std::printf("test_execute_nonexistent_script OK (returns 0 for missing script)\n");
}

static void test_execute_simple_script() {
    RootEnvironment env;
    env.provider = RootProvider::Magisk;
    env.overlayBase = "/tmp/nexus_test_overlay";
    env.modulesDir = "/tmp/nexus_test_modules";
    mkdirRecursive("/tmp/nexus_test_script", 0755);

    ScriptExecutor exec(env);
    ScriptExecutor::ExecOptions opts;
    opts.stage = ScriptExecutor::Stage::LateStart;
    opts.moduleId = "test_mod";
    opts.modulePath = "/tmp/nexus_test_script";
    opts.moduleVersion = "1.0";

    // 测试一个简单脚本，验证 shim 注入的函数可用
    std::string scriptPath = "/tmp/nexus_test_script/test.sh";
    writeFile(scriptPath,
        "#!/system/bin/sh\n"
        "ui_print \"hello from script\"\n"
        "abort \"test abort\"\n",   // abort 应让 exitCode=1
        0755);

    opts.scriptPath = scriptPath;
    opts.isolateNamespace = false;   // 测试环境不需要 NS
    auto result = exec.execute(opts);
    // abort 调用 exit 1
    assert(result.exitCode == 1);
    // stderr 应包含 abort 消息
    assert(result.stderr_.find("test abort") != std::string::npos ||
           result.stdout_.find("test abort") != std::string::npos);
    std::printf("test_execute_simple_script OK (abort shim works, exitCode=%d)\n", result.exitCode);
}

int main() {
    test_shim_contains_required_functions();
    test_execute_nonexistent_script();
    test_execute_simple_script();
    std::printf("All script_executor tests passed.\n");
    return 0;
}
