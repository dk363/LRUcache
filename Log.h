#pragma once
#include <iostream>
#include <ctime>
#include <string>
#include <mutex>

enum class LogLevel {
    ERROR,
    WARN,
    INFO,
    DEBUG
};

// 修复：localtime_r参数顺序（Linux正确）
inline std::string getCurrentTime() {
    time_t now = time(nullptr);
    tm localTime;
#ifdef _WIN32
    localtime_s(&localTime, &now);
#else
    localtime_r(&now, &localTime);  // 正确顺序：(time_t*, tm*)
#endif
    char buf[64] = {0};
    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &localTime);
    return buf;
}

inline std::mutex log_mutex;

// 关键：宏重命名为LOG_CACHE，避免与GTest的LOG宏冲突
#define LOG_CACHE(level, msg) do { \
    std::lock_guard<std::mutex> lock(log_mutex); \
    std::cerr << "[" << #level << "] " \
              << getCurrentTime() << " " \
              << __FILE__ << ":" << __LINE__ << " " \
              << msg << std::endl; \
} while(0)

// 对应重命名日志宏
#define LOG_ERROR_CACHE(msg) LOG_CACHE(ERROR, msg)
#define LOG_WARN_CACHE(msg) LOG_CACHE(WARN, msg)
#define LOG_INFO_CACHE(msg) LOG_CACHE(INFO, msg)
#define LOG_DEBUG_CACHE(msg) LOG_CACHE(DEBUG, msg)