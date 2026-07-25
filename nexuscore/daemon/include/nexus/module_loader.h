#pragma once

#include "nexus/types.h"
#include "nexus/util.h"
#include <memory>
#include <vector>
#include <functional>

namespace nexus {

// 模块加载器：扫描 modulesDir、解析 manifest.json、按 priority 排序、capabilities 校验
//
// 重要：本类不执行脚本（由 ScriptExecutor 负责），不挂载文件（由 IFileSystemInterceptor 负责）。
// 它只负责"找到模块、解析清单、返回排序后的列表 + 校验报告"。
class ModuleLoader {
public:
    ModuleLoader(const RootEnvironment& env, std::string modulesDir)
        : env_(env), modulesDir_(std::move(modulesDir)) {}

    struct LoadedModule {
        ModuleManifest manifest;
        std::string path;              // /data/adb/nexuscore/modules/<id>
        bool hasPostFsData = false;
        bool hasService = false;
        bool hasCustomize = false;
        bool hasUninstall = false;
        bool hasVerify = false;
        std::vector<std::string> systemFiles;   // system/ 下的相对路径
    };

    // 扫描 modulesDir，解析所有 manifest.json
    // 失败：modulesDir 不存在或不可读（返回 Err::NotFound）
    Result<std::vector<LoadedModule>> scanModules();

    // 校验单个模块的 capabilities 声明
    // 返回校验报告：未声明能力但存在对应脚本/挂载 → 警告但跳过执行
    struct ValidationReport {
        std::vector<std::string> warnings;
        std::vector<std::string> errors;
        bool ok() const { return errors.empty(); }
    };
    ValidationReport validate(const LoadedModule& m);

    // 列出模块的 system/ 文件，构造 MountTarget 列表
    std::vector<MountTarget> collectMountTargets(const LoadedModule& m);

private:
    const RootEnvironment& env_;
    std::string modulesDir_;

    // 解析单个 manifest.json
    Result<ModuleManifest> parseManifest(const std::string& path);

    // 校验 manifest id 正则 ^[a-z][a-z0-9_]{2,63}$
    bool isValidId(const std::string& id);

    // 列出目录下所有 system/ 文件（相对 system/ 的路径）
    std::vector<std::string> listSystemFiles(const std::string& modulePath);
};

} // namespace nexus
