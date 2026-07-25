#pragma once

#include <string>

namespace nexus {

/// 权限级别
enum class PermissionLevel {
    NONE,     // 无权限
    SHELL,    // shell 权限（非 root）
    ROOT,     // root 权限
};

/// 权限提供者接口
///
/// 抽象层设计：不同 Root 后端（Magisk / KSU / APatch / NexusCore 自身）
/// 实现不同的 PermissionProvider，daemon 通过统一接口调用。
///
/// 当前阶段（用户态核心框架）：
///   - NoRootProvider: 不执行任何 root 操作，仅记录
///
/// 未来阶段（独立 Root 框架）：
///   - NexusRootProvider: 通过 boot patch 获取 root
///   - MagiskProvider: 代理 Magisk 的 root
///   - KsuProvider: 代理 KernelSU 的 root
class PermissionProvider {
public:
    virtual ~PermissionProvider() = default;

    /// 返回当前权限级别
    virtual PermissionLevel level() = 0;

    /// 执行命令（需要 root 权限的操作）
    /// @return true 表示执行成功
    virtual bool execute(const std::string& command) = 0;

    /// 返回提供者名称（如 "nexus" / "magisk" / "ksu" / "none"）
    virtual std::string name() = 0;
};

/// 无 Root 权限提供者（当前阶段默认）
///
/// - 如果 daemon 以 root 运行，level() 返回 ROOT
/// - 如果 daemon 以非 root 运行，level() 返回 NONE
/// - execute() 在用户态阶段禁止越权，始终返回 false
class NoRootProvider : public PermissionProvider {
public:
    PermissionLevel level() override;
    bool execute(const std::string& command) override;
    std::string name() override;
};

} // namespace nexus

