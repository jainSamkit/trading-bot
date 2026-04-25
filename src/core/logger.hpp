#pragma once
#include <cstdint>
#include <cstdio>
#include <ctime>
#include <cstdarg>

// Simple synchronous logger. Thread-safe: fprintf to stderr is POSIX-atomic per line.
// Usage:
//   LOG_INFO("oms",    "channel valid, orders=%d", count);
//   LOG_WARN("reconcile", "diff detected for order %lu", id);
//   LOG_ERROR("ws",    "seq gap: expected %lu got %lu", expected, got);

enum class LogLevel : uint8_t { DEBUG, INFO, WARN, ERROR };

namespace detail {

inline const char* level_str(LogLevel l) {
    switch (l) {
        case LogLevel::DEBUG: return "DEBUG";
        case LogLevel::INFO:  return "INFO ";
        case LogLevel::WARN:  return "WARN ";
        case LogLevel::ERROR: return "ERROR";
    }
    return "?????";
}

inline void log_impl(LogLevel level, const char* component, const char* fmt, ...) {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);

    struct tm t;
    localtime_r(&ts.tv_sec, &t);

    char msg[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(msg, sizeof(msg), fmt, ap);
    va_end(ap);

    fprintf(stderr, "[%02d:%02d:%02d.%03ld] [%s] [%-12s] %s\n",
            t.tm_hour, t.tm_min, t.tm_sec,
            ts.tv_nsec / 1000000L,
            level_str(level),
            component,
            msg);
}

} // namespace detail

#define LOG_DEBUG(comp, fmt, ...) ::detail::log_impl(LogLevel::DEBUG, comp, fmt, ##__VA_ARGS__)
#define LOG_INFO(comp,  fmt, ...) ::detail::log_impl(LogLevel::INFO,  comp, fmt, ##__VA_ARGS__)
#define LOG_WARN(comp,  fmt, ...) ::detail::log_impl(LogLevel::WARN,  comp, fmt, ##__VA_ARGS__)
#define LOG_ERROR(comp, fmt, ...) ::detail::log_impl(LogLevel::ERROR, comp, fmt, ##__VA_ARGS__)
