#ifndef ENGINE_LOG_HPP
#define ENGINE_LOG_HPP

// Minimal logging utility: just enough to make GL errors and lifecycle
// events (window created, context info, shutdown) visible and consistently
// formatted, not a general-purpose logging library. Every call goes through
// logMessage() so all output shares one "[HH:MM:SS] [LEVEL] message" format;
// LOG_INFO/LOG_WARN/LOG_ERROR below are just convenience wrappers over it.

#include <chrono>
#include <cstdio>
#include <ctime>
#include <string>

namespace engine {

enum class LogLevel { Info, Warn, Error };

inline const char* logLevelLabel(LogLevel level) {
    switch (level) {
        case LogLevel::Info:
            return "INFO";
        case LogLevel::Warn:
            return "WARN";
        case LogLevel::Error:
            return "ERROR";
    }
    return "?";
}

inline void logMessage(LogLevel level, const std::string& message) {
    const std::time_t now = std::time(nullptr);
    std::tm tmBuf{};
#if defined(_WIN32)
    localtime_s(&tmBuf, &now);
#else
    localtime_r(&now, &tmBuf);
#endif
    char timeBuf[16];
    std::strftime(timeBuf, sizeof(timeBuf), "%H:%M:%S", &tmBuf);

    // Errors/warnings go to stderr so they're visible/greppable separately
    // from normal informational output on stdout.
    std::FILE* stream = (level == LogLevel::Error || level == LogLevel::Warn) ? stderr : stdout;
    std::fprintf(stream, "[%s] [%s] %s\n", timeBuf, logLevelLabel(level), message.c_str());
}

}  // namespace engine

#define LOG_INFO(msg) ::engine::logMessage(::engine::LogLevel::Info, (msg))
#define LOG_WARN(msg) ::engine::logMessage(::engine::LogLevel::Warn, (msg))
#define LOG_ERROR(msg) ::engine::logMessage(::engine::LogLevel::Error, (msg))

#endif  // ENGINE_LOG_HPP
