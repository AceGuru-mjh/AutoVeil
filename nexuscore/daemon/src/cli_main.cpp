// nexuscli - 模块脚本可调用的 CLI 工具
//
// 用法：
//   nexuscli emit EVENT_NAME --module=<id> --data="key=val,..."
//   nexuscli log <level> <tag> <message>
//   nexuscli status
//
// 通过 UDS 连接本地 daemon，转发请求。

#include "nexus/log.h"
#include "nexus/ipc/codec.h"
#include "nexus/util.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

using namespace nexus;

static void printUsage() {
    std::cerr <<
        "nexuscli " NEXUS_VERSION "\n"
        "Usage:\n"
        "  nexuscli emit EVENT_NAME [--module=ID] [--data=k=v,...]\n"
        "  nexuscli log <level:VDIWE> <tag> <message>\n"
        "  nexuscli status\n"
        "  nexuscli --help\n";
}

static int connectToDaemon() {
    int fd = ::socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (fd < 0) return -1;
    struct sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    ::strncpy(addr.sun_path, "/dev/socket/nexusd.sock", sizeof(addr.sun_path) - 1);
    if (::connect(fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        ::close(fd);
        return -1;
    }
    return fd;
}

int main(int argc, char** argv) {
    if (argc < 2) {
        printUsage();
        return 1;
    }
    std::string cmd = argv[1];

    if (cmd == "--help" || cmd == "-h") {
        printUsage();
        return 0;
    }

    int fd = connectToDaemon();
    if (fd < 0) {
        std::cerr << "nexuscli: cannot connect to daemon\n";
        return 2;
    }

    if (cmd == "emit") {
        if (argc < 3) {
            printUsage();
            return 1;
        }
        std::string eventName = argv[2];
        std::string moduleId;
        std::string data;
        for (int i = 3; i < argc; ++i) {
            std::string a = argv[i];
            if (a.rfind("--module=", 0) == 0) moduleId = a.substr(9);
            else if (a.rfind("--data=", 0) == 0) data = a.substr(7);
        }

        ipc::Encoder enc;
        enc.putU32(0);   // seq=0 表示事件
        enc.putStr("EMIT_EVENT");
        enc.putStr(eventName);
        enc.putStr(moduleId);
        enc.putStr(data);
        enc.putEnd();
        auto payload = std::move(enc).bytes();
        bool ok = ipc::writeFrame(fd, payload);
        ::close(fd);
        return ok ? 0 : 3;
    }

    if (cmd == "log") {
        if (argc < 5) {
            printUsage();
            return 1;
        }
        std::string levelStr = argv[2];
        std::string tag = argv[3];
        std::string msg = argv[4];
        int level = 2;   // Info
        if (levelStr == "V") level = 0;
        else if (levelStr == "D") level = 1;
        else if (levelStr == "I") level = 2;
        else if (levelStr == "W") level = 3;
        else if (levelStr == "E") level = 4;

        ipc::Encoder enc;
        enc.putU32(0);
        enc.putStr("LOG_LINE");
        enc.putU32((uint32_t)level);
        enc.putStr(tag);
        enc.putStr(msg);
        enc.putEnd();
        auto payload = std::move(enc).bytes();
        bool ok = ipc::writeFrame(fd, payload);
        ::close(fd);
        return ok ? 0 : 3;
    }

    if (cmd == "status") {
        ipc::Encoder enc;
        enc.putU32(1);   // seq=1
        enc.putU32(2);   // type=GetStatus
        enc.putEnd();
        auto payload = std::move(enc).bytes();
        if (!ipc::writeFrame(fd, payload)) {
            ::close(fd);
            return 3;
        }
        auto resp = ipc::readFrame(fd);
        ::close(fd);
        if (!resp) return 4;
        std::cout << "daemon status received (" << resp->size() << " bytes)\n";
        return 0;
    }

    printUsage();
    return 1;
}
