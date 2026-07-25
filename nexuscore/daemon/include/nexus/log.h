#pragma once

#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <string>
#include <string_view>

namespace nexus {
namespace log {

// 日志级别（与 IPC LogLine.level 对齐）
enum class Level : uint32_t {
    Verbose = 0,
    Debug   = 1,
    Info    = 2,
    Warn    = 3,
    Error   = 4,
};

// 写入本地日志文件（/data/adb/nexuscore/logs/nexusd.log）+ logcat + 内存事件总线
void init(std::string_view logDir);
void shutdown();

// 设置最低输出级别（默认 Info）
void setMinLevel(Level level);

// 格式化日志
void write(Level level, const char* tag, const char* fmt, ...);
void vwrite(Level level, const char* tag, const char* fmt, va_list ap);

// 便捷宏
#define NX_LOG_V(tag, fmt, ...) ::nexus::log::write(::nexus::log::Level::Verbose, tag, fmt, ##__VA_ARGS__)
#define NX_LOG_D(tag, fmt, ...) ::nexus::log::write(::nexus::log::Level::Debug,   tag, fmt, ##__VA_ARGS__)
#define NX_LOG_I(tag, fmt, ...) ::nexus::log::write(::nexus::log::Level::Info,    tag, fmt, ##__VA_ARGS__)
#define NX_LOG_W(tag, fmt, ...) ::nexus::log::write(::nexus::log::Level::Warn,    tag, fmt, ##__VA_ARGS__)
#define NX_LOG_E(tag, fmt, ...) ::nexus::log::write(::nexus::log::Level::Error,   tag, fmt, ##__VA_ARGS__)

} // namespace log
} // namespace nexus
