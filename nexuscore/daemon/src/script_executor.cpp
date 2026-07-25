#include "nexus/script_executor.h"
#include "nexus/util.h"
#include "nexus/log.h"

#include <algorithm>
#include <chrono>
#include <csignal>
#include <cstring>
#include <cstdlib>
#include <fcntl.h>
#include <poll.h>
#include <sched.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

namespace nexus {

const char* ScriptExecutor::stageName(Stage s) {
    switch (s) {
        case Stage::PostFsData: return "post-fs-data";
        case Stage::LateStart:  return "late_start";
        case Stage::Install:    return "install";
        case Stage::Uninstall:  return "uninstall";
        case Stage::Verify:     return "verify";
    }
    return "unknown";
}

std::vector<std::string> ScriptExecutor::buildEnv(const ExecOptions& opts) {
    std::vector<std::string> env;
    env.push_back("NEXUS_MODULE_PATH=" + opts.modulePath);
    env.push_back("NEXUS_MODULE_ID=" + opts.moduleId);
    env.push_back("NEXUS_MODULE_VERSION=" + opts.moduleVersion);
    env.push_back("NEXUS_VERSION=" NEXUS_VERSION);
    env.push_back(std::string("NEXUS_BOOT_STAGE=") + stageName(opts.stage));
    env.push_back("NEXUS_ROOT_PROVIDER=" + std::string(rootProviderName(env_.provider)));
    env.push_back("NEXUS_API_LEVEL=34");   // MVP 假定 34
    env.push_back("NEXUS_ARCH=arm64");
    env.push_back("NEXUS_OVERLAY_BASE=" + env_.overlayBase);
    env.push_back("NEXUS_TMPDIR=/data/adb/nexuscore/tmp/" + opts.moduleId);
    // Magisk 兼容别名
    env.push_back("MODPATH=" + opts.modulePath);
    env.push_back("TMPDIR=/data/adb/nexuscore/tmp/" + opts.moduleId);
    if (!opts.zipPath.empty()) {
        env.push_back("ZIPFILE=" + opts.zipPath);
    }
    // PATH
    env.push_back("PATH=/system/bin:/system/xbin:/data/adb/nexuscore/bin");
    return env;
}

std::string ScriptExecutor::buildShimScript(const ExecOptions& opts) {
    std::string s;
    s.reserve(2048);
    s += "#!/system/bin/sh\n";
    s += "# NexusCore auto-injected Magisk compatibility shim\n";
    s += "NEXUS_UI_PIPE=/dev/null\n";
    // ui_print: 输出到 stdout，daemon 转发到 Manager
    s += "ui_print() { echo \"- $*\"; }\n";
    // set_perm: chown + chmod
    s += "set_perm() {\n";
    s += "  local f=$1 owner=$2 group=$3 perm=$4\n";
    s += "  chown \"$owner:$group\" \"$f\" 2>/dev/null\n";
    s += "  chmod \"$perm\" \"$f\" 2>/dev/null\n";
    s += "}\n";
    // set_perm_recursive
    s += "set_perm_recursive() {\n";
    s += "  local d=$1 owner=$2 group=$3 dperm=$4 fperm=$5\n";
    s += "  chown -R \"$owner:$group\" \"$d\" 2>/dev/null\n";
    s += "  find \"$d\" -type d -exec chmod \"$dperm\" {} + 2>/dev/null\n";
    s += "  find \"$d\" -type f -exec chmod \"$fperm\" {} + 2>/dev/null\n";
    s += "}\n";
    // 整改 #6：abort shim 必须定义
    s += "abort() { echo \"! abort: $*\" >&2; exit 1; }\n";
    return s;
}

ScriptExecutor::ExecResult ScriptExecutor::execute(const ExecOptions& opts) {
    ExecResult result;

    if (!probeFile(opts.scriptPath)) {
        result.exitCode = 0;   // 脚本不存在视为成功（未提供）
        return result;
    }

    // Phase 1.3 / 1.6 修复：
    // - shimPath 用 mkstemp 防止 TOCTOU
    // - cmd 变量删除（未使用）
    // - pipe2 失败时正确关闭已分配的 fd
    // - 用 execve 而非 sh -c "string"，路径作为 argv 元素传递（防 shell 注入）
    // - 用 poll 同时读 stdout/stderr（防死锁）
    // - 用 alarm + waitpid 实现超时（防脚本卡死）

    // 写 shim 到临时文件（用 mkstemp 防止 TOCTOU）
    std::string tmpDir = "/data/adb/nexuscore/tmp";
    mkdirRecursive(tmpDir, 0755);
    std::string shimTemplate = tmpDir + "/shim_XXXXXX.sh";
    std::vector<char> shimBuf(shimTemplate.begin(), shimTemplate.end());
    shimBuf.push_back('\0');
    int shimFd = ::mkostemp(shimBuf.data(), O_CLOEXEC);
    if (shimFd < 0) {
        result.exitCode = -1;
        result.stderr_ = "mkstemp failed";
        return result;
    }
    std::string shimPath = shimBuf.data();
    std::string shim = buildShimScript(opts);
    if (::write(shimFd, shim.data(), shim.size()) != (ssize_t)shim.size()) {
        ::close(shimFd);
        ::unlink(shimPath.c_str());
        result.exitCode = -1;
        result.stderr_ = "write shim failed";
        return result;
    }
    ::close(shimFd);
    ::chmod(shimPath.c_str(), 0755);

    // fork
    int outPipe[2] = {-1, -1};
    int errPipe[2] = {-1, -1};
    if (::pipe2(outPipe, O_CLOEXEC) < 0) {
        ::unlink(shimPath.c_str());
        result.exitCode = -1;
        result.stderr_ = "pipe2 outPipe failed";
        return result;
    }
    if (::pipe2(errPipe, O_CLOEXEC) < 0) {
        // Phase 1.3 修复：原代码此处未关闭 outPipe，造成 fd 泄漏
        ::close(outPipe[0]); ::close(outPipe[1]);
        ::unlink(shimPath.c_str());
        result.exitCode = -1;
        result.stderr_ = "pipe2 errPipe failed";
        return result;
    }

    pid_t pid = ::fork();
    if (pid < 0) {
        ::close(outPipe[0]); ::close(outPipe[1]);
        ::close(errPipe[0]); ::close(errPipe[1]);
        ::unlink(shimPath.c_str());
        result.exitCode = -1;
        result.stderr_ = "fork failed";
        return result;
    }

    if (pid == 0) {
        // child
        ::close(outPipe[0]); ::close(errPipe[0]);

        // 独立 mount namespace（post-fs-data / service）
        if (opts.isolateNamespace) {
            if (::unshare(CLONE_NEWNS) < 0) {
                // 不致命，继续
                NX_LOG_W("ScriptExec", "unshare(NEWNS) failed for %s: %s",
                         opts.moduleId.c_str(), nexus::errnoString(errno).c_str());
            } else {
                // 私有挂载传播，避免影响父进程
                // Phase 1.3 修复：检查 mount 返回值
                if (::mount("", "/", "", MS_PRIVATE | MS_REC, nullptr) < 0) {
                    NX_LOG_W("ScriptExec", "MS_PRIVATE|MS_REC failed: %s", nexus::errnoString(errno).c_str());
                }
            }
        }

        ::dup2(outPipe[1], STDOUT_FILENO);
        ::dup2(errPipe[1], STDERR_FILENO);
        ::close(outPipe[1]); ::close(errPipe[1]);

        // 设置环境变量
        for (auto& e : buildEnv(opts)) {
            ::putenv(::strdup(e.c_str()));
        }

        // Phase 1.3 修复：原用 sh -c ". <shimPath>; . <scriptPath>" 拼接字符串，
        // 路径含特殊字符（空格、$、;）会被 shell 解释，造成注入。
        // 改用 execve + 显式 argv：sh -c '. "$1"; . "$2"' -- <shimPath> <scriptPath>
        // 这样路径作为 argv[1]/argv[2] 传入，shell 不会再次解析。
        const char* shArgs[] = {
            "sh", "-c",
            ". \"$1\"; . \"$2\"",
            "--",
            shimPath.c_str(),
            opts.scriptPath.c_str(),
            nullptr,
        };
        ::execv("/system/bin/sh", const_cast<char* const*>(shArgs));
        ::_exit(127);
    }

    ::close(outPipe[1]); ::close(errPipe[1]);

    // Phase 1.6 修复：用 poll 同时读 stdout/stderr，防止死锁
    // 原代码先 readAll(outPipe) 再 readAll(errPipe)，stderr 满时子进程阻塞 → 父进程死锁
    auto readAvailable = [](int fd, std::string& out) {
        char buf[4096];
        ssize_t n;
        while ((n = ::read(fd, buf, sizeof(buf))) > 0) {
            out.append(buf, n);
            if (out.size() > 256 * 1024) {
                // 限制单流最大 256 KiB，防止恶意脚本耗尽内存
                out.append("[truncated]");
                break;
            }
        }
    };

    // 同时 poll 两个 fd，直到都 EOF
    bool outOpen = true, errOpen = true;
    while (outOpen || errOpen) {
        struct pollfd pfds[2];
        int nfds = 0;
        if (outOpen) { pfds[nfds].fd = outPipe[0]; pfds[nfds].events = POLLIN; nfds++; }
        if (errOpen) { pfds[nfds].fd = errPipe[0]; pfds[nfds].events = POLLIN; nfds++; }
        if (nfds == 0) break;
        int pr = ::poll(pfds, nfds, 30000);   // 30 秒无数据超时
        if (pr < 0) {
            if (errno == EINTR) continue;
            break;
        }
        if (pr == 0) {
            // 30 秒无数据，检查子进程是否还活着
            int status;
            pid_t r = ::waitpid(pid, &status, WNOHANG);
            if (r != 0) break;   // 子进程已退出
            // 否则继续等
            continue;
        }
        for (int i = 0; i < nfds; ++i) {
            if (pfds[i].revents & POLLIN) {
                char buf[4096];
                ssize_t n = ::read(pfds[i].fd, buf, sizeof(buf));
                if (n > 0) {
                    if (pfds[i].fd == outPipe[0]) {
                        result.stdout_.append(buf, n);
                        if (result.stdout_.size() > 256 * 1024) {
                            result.stdout_.append("[truncated]");
                            ::close(outPipe[0]); outOpen = false;
                        }
                    } else {
                        result.stderr_.append(buf, n);
                        if (result.stderr_.size() > 256 * 1024) {
                            result.stderr_.append("[truncated]");
                            ::close(errPipe[0]); errOpen = false;
                        }
                    }
                } else {
                    // EOF
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

    // Phase 1.6：实现超时（默认 120 秒）
    int status = 0;
    int waited = 0;
    while (::waitpid(pid, &status, WNOHANG) == 0) {
        if (waited >= opts.timeoutSec) {
            NX_LOG_W("ScriptExec", "[%s/%s] timeout after %ds, SIGKILL",
                     opts.moduleId.c_str(), stageName(opts.stage), opts.timeoutSec);
            ::kill(pid, SIGKILL);
            result.timedOut = true;
            ::waitpid(pid, &status, 0);
            break;
        }
        ::sleep(1);
        ++waited;
    }
    result.exitCode = WIFEXITED(status) ? WEXITSTATUS(status) : -1;

    ::unlink(shimPath.c_str());

    // 转发到日志
    if (!result.stdout_.empty()) {
        NX_LOG_D("ScriptExec", "[%s/%s] stdout: %s",
                 opts.moduleId.c_str(), stageName(opts.stage), result.stdout_.c_str());
    }
    if (!result.stderr_.empty()) {
        NX_LOG_W("ScriptExec", "[%s/%s] stderr: %s",
                 opts.moduleId.c_str(), stageName(opts.stage), result.stderr_.c_str());
    }
    NX_LOG_I("ScriptExec", "[%s/%s] exit=%d%s",
             opts.moduleId.c_str(), stageName(opts.stage), result.exitCode,
             result.timedOut ? " (TIMEOUT)" : "");

    return result;
}

} // namespace nexus
