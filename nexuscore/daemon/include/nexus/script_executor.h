#pragma once

#include "nexus/types.h"
#include "nexus/util.h"
#include <string>
#include <vector>
#include <unordered_map>

namespace nexus {

// 脚本执行器
//
// 职责：
// - 在独立 Mount Namespace 内执行 .sh 脚本（post-fs-data / service / customize / uninstall）
// - 注入 Magisk 兼容 shim：ui_print / set_perm / set_perm_recursive / abort
// - 注入环境变量：NEXUS_MODULE_PATH / NEXUS_MODULE_ID / NEXUS_VERSION / NEXUS_BOOT_STAGE 等
// - 120 秒超时，超时 SIGKILL
// - 收集 stdout/stderr 转发到日志
class ScriptExecutor {
public:
    enum class Stage {
        PostFsData,
        LateStart,
        Install,    // customize.sh
        Uninstall,
        Verify,     // verify.sh
    };

    static const char* stageName(Stage s);

    struct ExecOptions {
        Stage stage;
        std::string moduleId;
        std::string modulePath;      // /data/adb/nexuscore/modules/<id>
        std::string moduleVersion;
        std::string scriptPath;      // 绝对路径
        // Install 阶段额外：
        std::string zipPath;         // 模块 ZIP 路径（注入 ZIPFILE）
        // 共享 NS（customize/uninstall）vs 独立 NS（post-fs-data/service）
        bool isolateNamespace = true;
        int timeoutSec = 120;
    };

    struct ExecResult {
        int exitCode = -1;
        bool timedOut = false;
        std::string stdout_;
        std::string stderr_;
    };

    explicit ScriptExecutor(const RootEnvironment& env) : env_(env) {}

    // 执行脚本。exitCode=0 成功；1 一般失败；2 严重失败（标记模块 disabled）
    // 失败本身不抛错（避免 Bootloop），由调用方按 exitCode 决策。
    ExecResult execute(const ExecOptions& opts);

private:
    const RootEnvironment& env_;

    // 构造 Magisk 兼容 shim 脚本（写在临时文件，执行时 source）
    std::string buildShimScript(const ExecOptions& opts);

    // 构造环境变量列表
    std::vector<std::string> buildEnv(const ExecOptions& opts);
};

} // namespace nexus
