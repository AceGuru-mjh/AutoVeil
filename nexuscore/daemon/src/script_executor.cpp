#include "nexus/script_executor.h"
#include "nexus/util.h"
#include "nexus/log.h"

#include <chrono>
#include <csignal>
#include <cstring>
#include <fcntl.h>
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

    // 写 shim 到临时文件
    std::string tmpDir = "/data/adb/nexuscore/tmp";
    mkdirRecursive(tmpDir, 0755);
    std::string shimPath = tmpDir + "/shim_" + opts.moduleId + "_" +
                           std::to_string((long)::getpid()) + ".sh";
    std::string shim = buildShimScript(opts);
    if (!writeFile(shimPath, shim, 0755)) {
        result.exitCode = -1;
        result.stderr_ = "failed to write shim";
        return result;
    }

    // 构造完整命令：source shim; source script
    std::string cmd = "/system/bin/sh -c '"
                     ". " + shimPath + "; "
                     ". " + opts.scriptPath + "'";

    // fork
    int outPipe[2] = {-1, -1};
    int errPipe[2] = {-1, -1};
    if (::pipe2(outPipe, O_CLOEXEC) < 0 || ::pipe2(errPipe, O_CLOEXEC) < 0) {
        ::unlink(shimPath.c_str());
        result.exitCode = -1;
        result.stderr_ = "pipe2 failed";
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
                         opts.moduleId.c_str(), ::strerror(errno));
            } else {
                // 私有挂载传播，避免影响父进程
                ::mount("", "/", "", MS_PRIVATE | MS_REC, nullptr);
            }
        }

        ::dup2(outPipe[1], STDOUT_FILENO);
        ::dup2(errPipe[1], STDERR_FILENO);
        ::close(outPipe[1]); ::close(errPipe[1]);

        // 设置环境变量
        for (auto& e : buildEnv(opts)) {
            ::putenv(::strdup(e.c_str()));
        }

        ::execl("/system/bin/sh", "sh", "-c",
                (". " + shimPath + "; . " + opts.scriptPath).c_str(),
                nullptr);
        ::_exit(127);
    }

    ::close(outPipe[1]); ::close(errPipe[1]);

    // 读 stdout/stderr（同时记录到日志）
    // MVP 简化：先读完再 wait，生产应同时 epoll 两个 fd
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
    NX_LOG_I("ScriptExec", "[%s/%s] exit=%d",
             opts.moduleId.c_str(), stageName(opts.stage), result.exitCode);

    return result;
}

} // namespace nexus
