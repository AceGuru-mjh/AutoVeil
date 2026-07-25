// su 客户端二进制入口
//
// Phase 3：独立的 su 二进制，setuid root 但只做连接透传。
//
// 安装位置：/system/bin/su（setuid root, mode 6755）
// 工作流程：
//   1. 检查调用者身份
//   2. 连接到 /dev/socket/nexus_su.sock
//   3. 发送 hello 让 daemon 通过 SO_PEERCRED 获取身份
//   4. 等待 daemon 响应（G=授权，D=拒绝）
//   5. 若授权，双向透传 stdin/stdout 与 daemon PTY
//
// 安全模型：
//   - su 二进制 setuid root，但不在客户端执行 root 操作
//   - 真正的 root shell 由 nexusd (daemon) fork
//   - 客户端无法绕过 daemon 直接获取 root
//
// 使用方式：
//   su                           # 进入交互式 root shell
//   su -c "command"              # 执行单条命令
//   su com.example.app           # 切换到指定应用 uid

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <thread>

namespace {

int connectToDaemon() {
    int fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) return -1;
    struct sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    ::strncpy(addr.sun_path, "/dev/socket/nexus_su.sock", sizeof(addr.sun_path) - 1);
    if (::connect(fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        ::close(fd);
        return -1;
    }
    return fd;
}

void forwardStream(int fromFd, int toFd) {
    char buf[4096];
    ssize_t n;
    while ((n = ::read(fromFd, buf, sizeof(buf))) > 0) {
        ssize_t w = 0;
        while (w < n) {
            ssize_t r = ::write(toFd, buf + w, n - w);
            if (r < 0) break;
            w += r;
        }
    }
}

void printUsage() {
    std::fprintf(stderr,
        "NexusCore su client\n"
        "Usage:\n"
        "  su                          # 进入交互式 root shell\n"
        "  su -c 'command'             # 执行单条命令\n"
        "  su <package>                # 切换到指定应用 uid\n"
        "  su -v, --version            # 显示版本\n"
        "  su -h, --help               # 显示帮助\n");
}

} // anonymous namespace

int main(int argc, char** argv) {
    // 解析参数
    std::string command;
    std::string targetPkg;
    bool showVersion = false;
    bool showHelp = false;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "-c" && i + 1 < argc) {
            command = argv[++i];
        } else if (arg == "-v" || arg == "--version") {
            showVersion = true;
        } else if (arg == "-h" || arg == "--help") {
            showHelp = true;
        } else if (arg[0] != '-') {
            targetPkg = arg;
        }
    }

    if (showVersion) {
        std::printf("NexusCore su 1.0.0\n");
        return 0;
    }
    if (showHelp) {
        printUsage();
        return 0;
    }

    // 连接到 daemon
    int fd = connectToDaemon();
    if (fd < 0) {
        std::fprintf(stderr, "su: cannot connect to nexusd su daemon\n");
        std::fprintf(stderr, "    ensure NexusCore is installed and daemon is running\n");
        return 1;
    }

    // 发送 hello（daemon 通过 SO_PEERCRED 获取 uid/pid）
    char hello = 'S';
    if (::write(fd, &hello, 1) != 1) {
        std::fprintf(stderr, "su: failed to send hello\n");
        ::close(fd);
        return 1;
    }

    // 如果有 command，发送给 daemon
    if (!command.empty()) {
        uint32_t cmdLen = (uint32_t)command.size();
        ::write(fd, &cmdLen, 4);
        ::write(fd, command.data(), command.size());
    } else {
        uint32_t cmdLen = 0;
        ::write(fd, &cmdLen, 4);
    }

    // 等待响应
    char response = 0;
    if (::read(fd, &response, 1) != 1) {
        std::fprintf(stderr, "su: no response from daemon\n");
        ::close(fd);
        return 1;
    }

    if (response != 'G') {   // G=Granted, D=Denied
        std::fprintf(stderr, "su: permission denied\n");
        ::close(fd);
        return 1;
    }

    // 授权成功，双向透传
    // stdin → daemon, daemon → stdout
    std::thread stdinThread(forwardStream, STDIN_FILENO, fd);
    char buf[4096];
    ssize_t n;
    while ((n = ::read(fd, buf, sizeof(buf))) > 0) {
        ::write(STDOUT_FILENO, buf, n);
    }
    stdinThread.detach();
    ::close(fd);
    return 0;
}
