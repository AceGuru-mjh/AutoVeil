#include "nexus/log.h"
#include "nexus/util.h"

#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <ctime>
#include <fcntl.h>
#include <mutex>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>

#ifdef __ANDROID__
#include <android/log.h>
#endif

namespace nexus::log {

static std::mutex g_mu;
static int g_logFd = -1;
static Level g_minLevel = Level::Info;
static std::string g_logPath;

void init(std::string_view logDir) {
    std::lock_guard<std::mutex> lk(g_mu);
    g_logPath = std::string(logDir) + "/nexusd.log";
    mkdirRecursive(std::string(logDir), 0755);
    g_logFd = ::open(g_logPath.c_str(),
                     O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC, 0644);
    if (g_logFd < 0) {
        g_logFd = -1;   // 日志写不了不致命，继续运行
    }
}

void shutdown() {
    std::lock_guard<std::mutex> lk(g_mu);
    if (g_logFd >= 0) {
        ::close(g_logFd);
        g_logFd = -1;
    }
}

void setMinLevel(Level level) {
    std::lock_guard<std::mutex> lk(g_mu);
    g_minLevel = level;
}

static const char* levelStr(Level l) {
    switch (l) {
        case Level::Verbose: return "V";
        case Level::Debug:   return "D";
        case Level::Info:    return "I";
        case Level::Warn:    return "W";
        case Level::Error:   return "E";
    }
    return "?";
}

static const int androidPriority(Level l) {
#ifdef __ANDROID__
    switch (l) {
        case Level::Verbose: return ANDROID_LOG_VERBOSE;
        case Level::Debug:   return ANDROID_LOG_DEBUG;
        case Level::Info:    return ANDROID_LOG_INFO;
        case Level::Warn:    return ANDROID_LOG_WARN;
        case Level::Error:   return ANDROID_LOG_ERROR;
    }
    return ANDROID_LOG_DEFAULT;
#else
    (void)l;
    return 0;
#endif
}

void vwrite(Level level, const char* tag, const char* fmt, va_list ap) {
    {
        std::lock_guard<std::mutex> lk(g_mu);
        if (level < g_minLevel) return;
    }

    char buf[1024];
    int n = 0;

    // 时间戳前缀
    auto now = std::chrono::system_clock::now();
    auto t = std::chrono::system_clock::to_time_t(now);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()) % 1000;
    struct tm tm{};
    ::localtime_r(&t, &tm);
    // Phase 1.7 修复：原代码未检查 snprintf 返回值，-1 会让 buf+n 越界
    int wrote = std::snprintf(buf + n, sizeof(buf) - n,
                              "[%04d-%02d-%02d %02d:%02d:%02d.%03d] [%s] [%s] ",
                              tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
                              tm.tm_hour, tm.tm_min, tm.tm_sec, (int)ms.count(),
                              levelStr(level), tag ? tag : "");
    if (wrote < 0) return;
    n += wrote;
    if (n >= (int)sizeof(buf)) n = sizeof(buf) - 1;

    // 用户消息
    wrote = std::vsnprintf(buf + n, sizeof(buf) - n, fmt, ap);
    if (wrote < 0) return;
    n += wrote;
    if (n >= (int)sizeof(buf)) n = sizeof(buf) - 1;
    buf[n] = '\0';
    if (n > 0 && buf[n - 1] != '\n') {
        if (n + 1 < (int)sizeof(buf)) {
            buf[n] = '\n';
            buf[n + 1] = '\0';
            n++;
        }
    }

    // 写日志文件
    {
        std::lock_guard<std::mutex> lk(g_mu);
        if (g_logFd >= 0) {
            ::write(g_logFd, buf, n);
        }
    }

    // 写 logcat（仅 Android 平台）
#ifdef __ANDROID__
    __android_log_print(androidPriority(level), tag ? tag : "nexusd", "%s", buf);
#else
    // host 测试时输出到 stderr
    std::fprintf(stderr, "%s", buf);
#endif
}

void write(Level level, const char* tag, const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vwrite(level, tag, fmt, ap);
    va_end(ap);
}

} // namespace nexus::log
