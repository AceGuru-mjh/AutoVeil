#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>
#include <optional>
#include <system_error>
#include <variant>

namespace nexus {

// ============ unexpect_t tag (替代 C++23 std::unexpected) ============
// Phase 1.1 修复：原代码用 std::unexpected 需要 C++23，但 CMake 设置 C++17。
// 改用 tag type + 构造函数重载，行为等价但不依赖 <expected>。
struct unexpect_t {};
inline constexpr unexpect_t unexpect{};

// ============ Result<T, E> 简化实现 ============
// C++23 的 std::expected 尚未广泛可用，自己实现一个最小版本。
// 用法：
//   Result<int> foo() { return 42; }
//   Result<int> bar() { return {unexpect, Err::IoError}; }
//
//   auto r = foo();
//   if (!r) { ... r.error() ... }
//   else { ... *r ... }

enum class Err : int {
    Ok = 0,
    IoError = 1,
    MountFailed = 2,
    InvalidArg = 3,
    Unauthorized = 4,
    Timeout = 5,
    NotFound = 6,
    AlreadyExists = 7,
    CapabilityDenied = 8,
    ScriptFailed = 9,
    ProtocolError = 10,
    Unsupported = 11,
};

const char* errString(Err e);

template <typename T, typename E = Err>
class Result {
public:
    // 成功构造
    Result(T v) : value_(std::move(v)), has_value_(true) {}

    // 失败构造（tag 方式，避免与成功构造歧义）
    Result(unexpect_t, E e) : error_(e), has_value_(false) {}

    // 兼容旧代码：直接传 E（标记为 explicit 避免隐式转换歧义）
    explicit Result(E e) : error_(e), has_value_(false) {}

    explicit operator bool() const { return has_value_; }
    bool has_value() const { return has_value_; }

    T& operator*() { return value_; }
    const T& operator*() const { return value_; }
    T* operator->() { return &value_; }
    const T* operator->() const { return &value_; }

    T& value() { return value_; }
    const T& value() const { return value_; }

    T value_or(T default_v) const {
        return has_value_ ? std::move(value_) : default_v;
    }

    E error() const { return error_; }

private:
    T value_{};
    E error_{};
    bool has_value_;
};

// void 特化
template <typename E>
class Result<void, E> {
public:
    // 成功构造
    Result() : has_value_(true) {}
    Result(unexpect_t, E e) : error_(e), has_value_(false) {}
    explicit Result(E e) : error_(e), has_value_(false) {}

    explicit operator bool() const { return has_value_; }
    bool has_value() const { return has_value_; }
    E error() const { return error_; }

private:
    E error_{};
    bool has_value_;
};

// ============ 基础类型 ============

struct MountTarget {
    std::string source;   // 模块版文件路径，如 /data/adb/nexuscore/modules/<id>/system/build.prop
    std::string target;   // 系统路径，如 /system/build.prop
    std::string moduleId; // 用于日志与回滚
};

enum class RootProvider {
    None,
    Magisk,
    KernelSU,
    APatch,
};

const char* rootProviderName(RootProvider p);

struct RootEnvironment {
    RootProvider provider = RootProvider::None;
    std::string providerVersion;
    std::string adbRootDir;       // /data/adb (Magisk) / /data/adb/ksu (KSU) / /data/adb/ap (APatch)
    std::string overlayBase;      // /data/adb/nexuscore/overlay
    std::string modulesDir;       // /data/adb/nexuscore/modules
    std::string sepolicyPath;     // /sys/fs/selinux/policy
    bool sepolicyWritable = false;       // ::access(W_OK) == 0
    bool overlayFsAvailable = false;     // /proc/filesystems 含 overlay
    bool fuseAvailable = false;          // /proc/filesystems 含 fuse
    bool dynamicPartitions = false;      // /proc/mounts 含 /dev/block/dm-
};

// 模块清单（DMM）
struct ModuleManifest {
    std::string id;
    std::string name;
    std::string version;
    int versionCode = 0;
    std::string author;
    std::string description;
    std::string minNexusVersion;
    int priority = 0;
    bool enabled = true;
    std::vector<std::string> capabilities;
    std::string homepage;
    // intents/permissions MVP 不解析
};

// IPC 消息类型（手写编解码，不依赖完整 protobuf）
struct LogLine {
    uint32_t level;   // 0=V 1=D 2=I 3=W 4=E
    std::string tag;
    std::string msg;
    uint64_t timestampMs;
};

} // namespace nexus
