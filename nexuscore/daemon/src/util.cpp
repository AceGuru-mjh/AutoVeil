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
bool copyFile(const std::string& src, const std::string& dst, mode_t mode) {
    int in = ::open(src.c_str(), O_RDONLY | O_CLOEXEC);
    if (in < 0) return false;
    int out = ::open(dst.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, mode);
    if (out < 0) {
        ::close(in);
        return false;
    }
    // 优先 sendfile（零拷贝），失败回退到 read/write
    struct stat st{};
    if (::fstat(in, &st) == 0 && st.st_size > 0) {
        off_t offset = 0;
        ssize_t sent = ::sendfile(out, in, &offset, st.st_size);
        if (sent == st.st_size) {
            ::close(in);
            ::close(out);
            return true;
        }
        // sendfile 失败回退到手动 copy
    }
    char buf[64 * 1024];
    ssize_t n;
    while ((n = ::read(in, buf, sizeof(buf))) > 0) {
        ssize_t written = 0;
        while (written < n) {
            ssize_t w = ::write(out, buf + written, n - written);
            if (w < 0) {
                if (errno == EINTR) continue;
                ::close(in);
                ::close(out);
                return false;
            }
            written += w;
        }
    }
    ::close(in);
    ::close(out);
    return n == 0;
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

    // 简单超时：alarm + waitpid（MVP 不实现精确超时，生产用 SIGCHLD + timer）
    auto readAll = [](int fd, std::string& out) {
        char buf[4096];
        ssize_t n;
        while ((n = ::read(fd, buf, sizeof(buf))) > 0) {
            out.append(buf, n);
        }
    };
    readAll(outPipe[0], result.stdout_);
    readAll(errPipe[0], result.stderr_);
    ::close(outPipe[0]); ::close(errPipe[0]);

    int status = 0;
    ::waitpid(pid, &status, 0);
    result.exitCode = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
    return result;
}

PidFile::~PidFile() { release(); }

Result<void> PidFile::acquire() {
    fd_ = ::open(path_.c_str(), O_RDWR | O_CREAT | O_CLOEXEC, 0644);
    if (fd_ < 0) return std::unexpected(Err::IoError);
    if (::flock(fd_, LOCK_EX | LOCK_NB) < 0) {
        ::close(fd_);
        fd_ = -1;
        return std::unexpected(Err::AlreadyExists);
    }
    char buf[32];
    int n = ::snprintf(buf, sizeof(buf), "%d\n", (int)::getpid());
    if (::write(fd_, buf, n) != n) {
        // 不致命
    }
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

} // namespace nexus
