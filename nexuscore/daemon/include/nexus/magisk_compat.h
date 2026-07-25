#pragma once

#include "nexus/types.h"
#include "nexus/util.h"
#include "nexus/module_loader.h"
#include <string>
#include <vector>

namespace nexus {

/// Magisk 兼容层
///
/// Phase 5：让现有 Magisk 模块能在 NexusCore 上运行（不抄 Magisk 实现）。
///
/// 兼容项：
/// 1. module.prop 解析（KV 格式 → DMM JSON 转换）
/// 2. system/ 目录挂载（与 DMM MOUNT_FILESYSTEM 等价）
/// 3. post-fs-data.sh / service.sh / customize.sh / uninstall.sh
/// 4. ui_print / set_perm / set_perm_recursive / abort 函数 shim
/// 5. MODPATH / TMPDIR / ZIPFILE 环境变量
/// 6. SKIPUNZIP 变量
///
/// 不兼容项（明确告知用户）：
/// - Zygisk 模块（.so 注入 zygote）→ 用 NexusHook 替代
/// - update-binary 自定义安装器
/// - system.prop 自动合并（用 system/build.prop 显式覆盖）
/// - sepolicy.sh 自动执行（用 capabilities 声明）
/// - MagiskHide / DenyList（用 NexusHook DenyList 替代）
class MagiskCompat {
public:
    /// 解析 Magisk module.prop（KV 格式）
    ///
    /// 格式：
    ///   id=module_id
    ///   name=Module Name
    ///   version=v1.0.0
    ///   versionCode=1
    ///   author=author
    ///   description=description
    ///
    /// @return ModuleManifest（转换后的 DMM 格式）
    static Result<ModuleManifest> parseModuleProp(const std::string& propPath);

    /// 把 Magisk module.prop 转换为 NexusCore manifest.json
    ///
    /// @param moduleDir 模块目录（含 module.prop）
    /// @return 转换后的 manifest.json 内容
    static Result<std::string> convertToManifest(const std::string& moduleDir);

    /// 检查目录是否为 Magisk 模块（有 module.prop）
    static bool isMagiskModule(const std::string& moduleDir);

    /// 把 Magisk 模块目录转换为 NexusCore 模块目录
    ///
    /// 1. 解析 module.prop 生成 manifest.json
    /// 2. 自动推断 capabilities：
    ///    - 有 *.sh 文件 → EXECUTE_SHELL
    ///    - 有 system/ 目录 → MOUNT_FILESYSTEM
    /// 3. 保留原 system/ 目录与脚本
    static Result<void> convertModule(const std::string& moduleDir);

    /// 列出目录下的 .sh 文件
    static std::vector<std::string> listShFiles(const std::string& dir);

    /// 检查目录是否有 system/ 子目录
    static bool hasSystemDir(const std::string& dir);

    /// Magisk shim 脚本（注入到 customize.sh / post-fs-data.sh 等）
    ///
    /// 这些 shim 函数在 NexusCore daemon 执行脚本时由 ScriptExecutor 自动注入，
    /// 这里提供的是 shim 的源代码（用于调试与文档）。
    static std::string getShimScript();
};

} // namespace nexus
