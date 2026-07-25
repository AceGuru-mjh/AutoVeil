#pragma once

#include "nexus/types.h"
#include <string>
#include <string_view>
#include <vector>

namespace nexus {

// 文件系统工具
bool probeFile(const std::string& path);
bool probeDir(const std::string& path);
bool mkdirRecursive(const std::string& path, mode_t mode = 0755);

// 整改：跨文件系统复制（替代原 spec 用 link 的错误做法）
// 从 src 读字节写到 dst，覆盖 dst；返回 false 表示 IO 错误。
bool copyFile(const std::string& src, const std::string& dst, mode_t mode = 0644);

// 读整个文件到 string；失败返回空 optional
std::optional<std::string> readFile(const std::string& path);

// 写 string 到文件（覆盖）；失败返回 false
bool writeFile(const std::string& path, std::string_view content, mode_t mode = 0644);

// 哈希（简单 FNV-1a 64bit，用于 stock/<hash> 路径生成）
std::string hashPath(const std::string& path);

// 内核能力探测
bool kernelSupports(std::string_view fsType);   // 检查 /proc/filesystems
bool isDynamicPartitions();                      // /proc/mounts 含 /dev/block/dm-

// 字符串工具
std::vector<std::string> split(const std::string& s, char delim);
std::string trim(const std::string& s);
bool startsWith(const std::string& s, std::string_view prefix);
bool endsWith(const std::string& s, std::string_view suffix);

// 命令执行（受控，仅在脚本沙盒外用）
struct ExecResult {
    int exitCode = -1;
    std::string stdout_;
    std::string stderr_;
};
ExecResult execCommand(const std::string& cmd, int timeoutSec = 30);

// PID 文件管理（防止双实例）
class PidFile {
public:
    explicit PidFile(std::string_view path) : path_(path) {}
    ~PidFile();
    Result<void> acquire();   // 写入 PID + flock LOCK_EX|LOCK_NB
    void release();
private:
    std::string path_;
    int fd_ = -1;
};

/// Phase 2: 线程安全的 errno → string 转换
/// strerror 是全局状态非线程安全，strerror_r 是线程安全版本
std::string errnoString(int err);

/// Phase 2: 路径安全校验，防止 shell 注入与路径遍历
/// 拒绝危险字符：' ; ` $ ( ) [ ] { } \ | & < > ..
bool isPathSafe(const std::string& path);

/// Phase 2: 校验模块 ID（与 ModuleLoader::isValidIdStatic 等价）
bool isValidModuleId(const std::string& id);

} // namespace nexus
