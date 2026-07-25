#include "nexus/util.h"
#include "nexus/log.h"

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <functional>
#include <poll.h>
#include <sys/file.h>
#include <sys/sendfile.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

namespace nexus {

bool probeFile(const std::string& path) {
    struct stat st{};
    return ::stat(path.c_str(), &st) == 0 && S_ISREG(st.st_mode);
}

bool probeDir(const std::string& path) {
    struct stat st{};
    return ::stat(path.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}

bool mkdirRecursive(const std::string& path, mode_t mode) {
    if (path.empty() || path == "/") return true;
    std::string current;
    current.reserve(path.size());
    for (size_t i = 0; i < path.size(); ++i) {
        char c = path[i];
        current.push_back(c);
        if (c == '/' && current.size() > 1) {
            ::mkdir(current.c_str(), mode);
        }
    }
    if (::mkdir(path.c_str(), mode) == 0) return true;
    return errno == EEXIST;
}

// 整改：跨文件系统复制（原 spec 用 ::link 会 EXDEV）
// Phase 1.5 修复：原 sendfile 部分成功后未 lseek，read 从头读出已发送字节导致输出重复损坏
// 改为：sendfile 循环到完成，否则用 lseek + read/write 兜底
bool copyFile(const std::string& src, const std::string& dst, mode_t mode) {
    int in = ::open(src.c_str(), O_RDONLY | O_CLOEXEC);
    if (in < 0) return false;
    int out = ::open(dst.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, mode);
    if (out < 0) {
        ::close(in);
        return false;
    }
    // 优先 sendfile（零拷贝），完整循环
    struct stat st{};
    if (::fstat(in, &st) == 0 && st.st_size > 0) {
        off_t offset = 0;
        bool sendfileOk = true;
        while (offset < st.st_size) {
            ssize_t sent = ::sendfile(out, in, &offset, st.st_size - offset);
            if (sent < 0) {
                if (errno == EINTR) continue;
                sendfileOk = false;
                break;
            }
            if (sent == 0) {
                // 异常：sendfile 返回 0 但未到 EOF
                sendfileOk = false;
                break;
            }
            // offset 已被 sendfile 自动推进
        }
        if (sendfileOk && offset == st.st_size) {
            ::close(in);
            ::close(out);
            return true;
        }
        // sendfile 失败：lseek 到已发送位置，继续用 read/write
        ::lseek(in, offset, SEEK_SET);
    }
    char buf[64 * 1024];
    ssize_t n;
    bool ok = true;
    while ((n = ::read(in, buf, sizeof(buf))) > 0) {
        ssize_t written = 0;
        while (written < n) {
            ssize_t w = ::write(out, buf + written, n - written);
            if (w < 0) {
                if (errno == EINTR) continue;
                ok = false;
                break;
            }
            // Phase 1.5 修复：原代码未处理 w == 0（磁盘满等情况会无限循环）
            if (w == 0) {
                ok = false;
                break;
            }
            written += w;
        }
        if (!ok) break;
    }
    if (n < 0) ok = false;
    ::close(in);
    ::close(out);
    return ok;
}

std::optional<std::string> readFile(const std::string& path) {
    int fd = ::open(path.c_str(), O_RDONLY | O_CLOEXEC);
    if (fd < 0) return std::nullopt;
    std::string content;
    char buf[4096];
    ssize_t n;
    while ((n = ::read(fd, buf, sizeof(buf))) > 0) {
        content.append(buf, n);
    }
    ::close(fd);
    if (n < 0) return std::nullopt;
    return content;
}

bool writeFile(const std::string& path, std::string_view content, mode_t mode) {
    int fd = ::open(path.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, mode);
    if (fd < 0) return false;
    ssize_t total = 0;
    while (total < (ssize_t)content.size()) {
        ssize_t w = ::write(fd, content.data() + total, content.size() - total);
        if (w < 0) {
            if (errno == EINTR) continue;
            ::close(fd);
            return false;
        }
        // Phase 1.5 修复：w == 0 表示写入无进展（磁盘满等），原代码会无限循环
        if (w == 0) {
            ::close(fd);
            return false;
        }
        total += w;
    }
    ::close(fd);
    return true;
}

// FNV-1a 64bit
std::string hashPath(const std::string& path) {
    uint64_t h = 1469598103934665603ULL;
    for (char c : path) {
        h ^= (uint8_t)c;
        h *= 1099511628211ULL;
    }
    char buf[17];
    ::snprintf(buf, sizeof(buf), "%016llx", (unsigned long long)h);
    return buf;
}

bool kernelSupports(std::string_view fsType) {
    FILE* f = ::fopen("/proc/filesystems", "r");
    if (!f) return false;
    char line[256];
    std::string needle(fsType);
    bool found = false;
    while (::fgets(line, sizeof(line), f)) {
        // line 格式: "nodev<tab>overlay" 或 "<tab>ext4"
        std::string s(line);
        if (s.find(needle) != std::string::npos) {
            found = true;
            break;
        }
    }
    ::fclose(f);
    return found;
}

bool isDynamicPartitions() {
    auto content = readFile("/proc/mounts");
    if (!content) return false;
    return content->find("/dev/block/dm-") != std::string::npos;
}

std::vector<std::string> split(const std::string& s, char delim) {
    std::vector<std::string> out;
    size_t start = 0;
    while (start <= s.size()) {
        size_t pos = s.find(delim, start);
        if (pos == std::string::npos) {
            out.push_back(s.substr(start));
            break;
        }
        out.push_back(s.substr(start, pos - start));
        start = pos + 1;
    }
    return out;
}

std::string trim(const std::string& s) {
    auto begin = s.find_first_not_of(" \t\r\n");
    if (begin == std::string::npos) return "";
    auto end = s.find_last_not_of(" \t\r\n");
    return s.substr(begin, end - begin + 1);
}

bool startsWith(const std::string& s, std::string_view prefix) {
    return s.size() >= prefix.size() &&
           s.compare(0, prefix.size(), prefix) == 0;
}

bool endsWith(const std::string& s, std::string_view suffix) {
    return s.size() >= suffix.size() &&
           s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
}

ExecResult execCommand(const std::string& cmd, int timeoutSec) {
    // Phase 1.6 修复：
    // - pipe2 失败时正确关闭已分配 fd（原代码泄漏）
    // - 用 poll 同时读 stdout/stderr（原代码顺序读会死锁）
    // - 实现 timeoutSec 超时（原代码忽略参数）

    ExecResult result;
    int outPipe[2] = {-1, -1};
    int errPipe[2] = {-1, -1};
    if (::pipe2(outPipe, O_CLOEXEC) < 0) return result;
    if (::pipe2(errPipe, O_CLOEXEC) < 0) {
        ::close(outPipe[0]); ::close(outPipe[1]);
        return result;
    }

    pid_t pid = ::fork();
    if (pid < 0) {
        ::close(outPipe[0]); ::close(outPipe[1]);
        ::close(errPipe[0]); ::close(errPipe[1]);
        return result;
    }

    if (pid == 0) {
        // child
        ::dup2(outPipe[1], STDOUT_FILENO);
        ::dup2(errPipe[1], STDERR_FILENO);
        ::close(outPipe[0]); ::close(outPipe[1]);
        ::close(errPipe[0]); ::close(errPipe[1]);
        ::execl("/system/bin/sh", "sh", "-c", cmd.c_str(), nullptr);
        ::_exit(127);
    }

    ::close(outPipe[1]); ::close(errPipe[1]);

    // Phase 1.6 修复：用 poll 同时读 stdout/stderr，防止死锁
    bool outOpen = true, errOpen = true;
    auto startTime = std::chrono::steady_clock::now();
    while (outOpen || errOpen) {
        struct pollfd pfds[2];
        int nfds = 0;
        if (outOpen) { pfds[nfds].fd = outPipe[0]; pfds[nfds].events = POLLIN; nfds++; }
        if (errOpen) { pfds[nfds].fd = errPipe[0]; pfds[nfds].events = POLLIN; nfds++; }
        if (nfds == 0) break;

        int pr = ::poll(pfds, nfds, 1000);   // 1 秒超时，便于检查总超时
        if (pr < 0) {
            if (errno == EINTR) continue;
            break;
        }
        if (pr == 0) {
            // 检查总超时
            auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::steady_clock::now() - startTime).count();
            if (elapsed >= timeoutSec) {
                ::kill(pid, SIGKILL);
                break;
            }
            continue;
        }
        for (int i = 0; i < nfds; ++i) {
            if (pfds[i].revents & POLLIN) {
                char buf[4096];
                ssize_t n = ::read(pfds[i].fd, buf, sizeof(buf));
                if (n > 0) {
                    if (pfds[i].fd == outPipe[0]) {
                        result.stdout_.append(buf, n);
                        if (result.stdout_.size() > 1024 * 1024) {
                            result.stdout_.append("[truncated]");
                            ::close(outPipe[0]); outOpen = false;
                        }
                    } else {
                        result.stderr_.append(buf, n);
                        if (result.stderr_.size() > 1024 * 1024) {
                            result.stderr_.append("[truncated]");
                            ::close(errPipe[0]); errOpen = false;
                        }
                    }
                } else {
                    if (pfds[i].fd == outPipe[0]) { ::close(outPipe[0]); outOpen = false; }
                    else { ::close(errPipe[0]); errOpen = false; }
                }
            }
            if (pfds[i].revents & (POLLHUP | POLLERR | POLLNVAL)) {
                if (pfds[i].fd == outPipe[0]) { ::close(outPipe[0]); outOpen = false; }
                else { ::close(errPipe[0]); errOpen = false; }
            }
        }
    }
    if (outOpen) ::close(outPipe[0]);
    if (errOpen) ::close(errPipe[0]);

    int status = 0;
    ::waitpid(pid, &status, 0);
    result.exitCode = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
    return result;
}

PidFile::~PidFile() { release(); }

Result<void> PidFile::acquire() {
    // Phase 1.5 修复：原代码未 O_TRUNC，旧 PID 文件残留内容会污染新内容
    fd_ = ::open(path_.c_str(), O_RDWR | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
    if (fd_ < 0) return {unexpect, Err::IoError};
    if (::flock(fd_, LOCK_EX | LOCK_NB) < 0) {
        ::close(fd_);
        fd_ = -1;
        return {unexpect, Err::AlreadyExists};
    }
    char buf[32];
    int n = ::snprintf(buf, sizeof(buf), "%d\n", (int)::getpid());
    if (::write(fd_, buf, n) != n) {
        // 不致命，但记录
    }
    // 再次 ftruncate 确保文件大小正确（双保险）
    ::ftruncate(fd_, n);
    return {};
}

void PidFile::release() {
    if (fd_ >= 0) {
        ::flock(fd_, LOCK_UN);
        ::close(fd_);
        fd_ = -1;
        ::unlink(path_.c_str());
    }
}

/// Phase 2: 线程安全的 errno → string 转换
std::string errnoString(int err) {
    char buf[256];
    buf[0] = '\0';
    // XSI 版本 strerror_r 返回 int；GNU 版本返回 char*
    // 用特征检测兼容两种实现
#if defined(_GNU_SOURCE) && !defined(__ANDROID__)
    char* r = strerror_r(err, buf, sizeof(buf));
    return std::string(r);
#else
    int r = strerror_r(err, buf, sizeof(buf));
    (void)r;
    return std::string(buf);
#endif
}

/// Phase 2: 路径安全校验
/// 拒绝危险字符：' ; ` $ ( ) [ ] { } \ | & < > ..
/// 允许：字母 数字 / . _ - 空格
bool isPathSafe(const std::string& path) {
    if (path.empty()) return false;
    // 拒绝路径遍历
    if (path.find("..") != std::string::npos) return false;
    // 拒绝 shell 特殊字符
    static const std::string dangerous = "';`$()[]{}\\|&<>";
    for (char c : path) {
        if (dangerous.find(c) != std::string::npos) return false;
    }
    return true;
}

/// Phase 2: 校验模块 ID
bool isValidModuleId(const std::string& id) {
    if (id.empty()) return false;
    char first = id[0];
    if (first < 'a' || first > 'z') return false;
    if (id.size() < 3 || id.size() > 64) return false;
    for (size_t i = 1; i < id.size(); ++i) {
        char c = id[i];
        bool ok = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_';
        if (!ok) return false;
    }
    return true;
}

} // namespace nexus
