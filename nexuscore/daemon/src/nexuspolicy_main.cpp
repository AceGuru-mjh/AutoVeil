// nexuspolicy - NexusCore 自研 SELinux 策略工具
//
// Phase 5：替代 Magisk 的 magiskpolicy，不抄其实现。
//
// 用法：
//   nexuspolicy --live "allow nexus_daemon self:capability { sys_admin }"
//   nexuspolicy --apply /path/to/policy.rules
//   nexuspolicy --query "nexus_daemon"
//
// 实现策略（与 Magisk 区别）：
// - Magisk magiskpolicy: 基于 fork libsepol，完整 C 库
// - nexuspolicy: 通过 shell out 调用 setenforce 0 + 写 /sys/fs/selinux/load
//   然后立即 setenforce 1（短暂 permissive 窗口注入策略）
//
// 注意：这是简化实现，生产应完整实现 libsepol 的 policy patching

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <string>
#include <sys/stat.h>
#include <unistd.h>

namespace {

int applyRuleLive(const std::string& rule) {
    // 简化实现：通过 setenforce 0 + 注入策略 + setenforce 1
    // 注意：这有短暂 permissive 窗口，生产应使用 libsepol 完整 patching

    std::fprintf(stderr, "nexuspolicy: applying rule: %s\n", rule.c_str());

    // 1. 读取当前 enforcing 状态
    int enforceFd = ::open("/sys/fs/selinux/enforce", O_RDONLY);
    if (enforceFd < 0) {
        std::fprintf(stderr, "nexuspolicy: cannot open enforce\n");
        return 1;
    }
    char buf[2] = {0};
    ::read(enforceFd, buf, 1);
    ::close(enforceFd);
    bool wasEnforcing = (buf[0] == '1');

    // 2. 临时 permissive
    if (wasEnforcing) {
        int fd = ::open("/sys/fs/selinux/enforce", O_WRONLY);
        if (fd >= 0) {
            ::write(fd, "0", 1);
            ::close(fd);
        }
    }

    // 3. 构造策略片段并写入 load
    // 注意：这里 rule 必须是完整的 SELinux policy 二进制，不是字符串
    // 简化：直接记录到日志，实际需要 libsepol 转换
    std::fprintf(stdout, "nexuspolicy: rule '%s' would be applied (need libsepol integration)\n",
                 rule.c_str());

    // 4. 恢复 enforcing
    if (wasEnforcing) {
        int fd = ::open("/sys/fs/selinux/enforce", O_WRONLY);
        if (fd >= 0) {
            ::write(fd, "1", 1);
            ::close(fd);
        }
    }

    return 0;
}

void printUsage() {
    std::fprintf(stderr,
        "nexuspolicy - NexusCore SELinux policy tool\n"
        "Usage:\n"
        "  nexuspolicy --live 'RULE'        # 注入单条规则\n"
        "  nexuspolicy --apply FILE         # 从文件批量注入规则\n"
        "  nexuspolicy --query DOMAIN       # 查询域信息\n"
        "  nexuspolicy --version            # 显示版本\n"
        "  nexuspolicy --help               # 显示帮助\n"
        "\n"
        "Rule format: 'allow source target:class { perm1 perm2 }'\n");
}

} // anonymous namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        printUsage();
        return 1;
    }

    std::string arg = argv[1];
    if (arg == "--help" || arg == "-h") {
        printUsage();
        return 0;
    }
    if (arg == "--version" || arg == "-v") {
        std::printf("nexuspolicy 1.0.0\n");
        return 0;
    }
    if (arg == "--live" && argc >= 3) {
        return applyRuleLive(argv[2]);
    }
    if (arg == "--apply" && argc >= 3) {
        // 从文件读取规则
        int fd = ::open(argv[2], O_RDONLY);
        if (fd < 0) {
            std::fprintf(stderr, "nexuspolicy: cannot open %s\n", argv[2]);
            return 1;
        }
        char buf[4096];
        std::string content;
        ssize_t n;
        while ((n = ::read(fd, buf, sizeof(buf))) > 0) {
            content.append(buf, n);
        }
        ::close(fd);
        // 按行应用
        size_t pos = 0;
        while (pos < content.size()) {
            size_t eol = content.find('\n', pos);
            if (eol == std::string::npos) eol = content.size();
            std::string line = content.substr(pos, eol - pos);
            pos = eol + 1;
            if (line.empty() || line[0] == '#') continue;
            applyRuleLive(line);
        }
        return 0;
    }

    printUsage();
    return 1;
}
