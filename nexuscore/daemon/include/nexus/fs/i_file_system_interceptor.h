#pragma once

#include "nexus/types.h"
#include "nexus/util.h"
#include <memory>
#include <vector>

namespace nexus {

// 文件系统拦截器接口
//
// 两种实现：
// - OverlayFsInterceptor：优先使用，支持多模块叠加
// - BindMountInterceptor：降级方案，单文件替换
// - NoopInterceptor：仅记录日志，不做任何 mount（read-only 模式）
//
// 由 FsDetector::select() 在启动期探测选择。
class IFileSystemInterceptor {
public:
    virtual ~IFileSystemInterceptor() = default;

    // 探测当前环境是否支持本实现（仅检测，不挂载）
    virtual Result<bool> detect(const RootEnvironment& env) = 0;

    // 将单个 source → target 绑定（MS_BIND 或 OverlayFS lowerdir）
    virtual Result<void> mountOverlay(const MountTarget& t) = 0;

    // 批量挂载（默认实现循环调用 mountOverlay，子类可优化）
    virtual Result<void> mountAll(const std::vector<MountTarget>& targets);

    // 卸载所有已挂载项（逆序）
    virtual Result<void> unmountAll() = 0;

    // 实现名（用于日志与 IPC 状态上报）
    virtual std::string_view implName() const = 0;

protected:
    std::vector<std::string> mounted_;
};

} // namespace nexus
