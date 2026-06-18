#ifndef TREBUCHET_LOGGER_H
#define TREBUCHET_LOGGER_H

#include <string>
#include <memory>

namespace trebuchet {

enum class LogLevel {
    TRACE,
    DEBUG,
    INFO,
    WARN,
    ERROR,
    CRITICAL
};

class Logger {
public:
    static void init(const std::string& log_dir = "logs",
                     const std::string& file_name = "trebuchet.log",
                     LogLevel console_level = LogLevel::INFO,
                     LogLevel file_level = LogLevel::DEBUG);

    static void setLevel(LogLevel level);

    static void log(LogLevel level, const std::string& message,
                    const std::string& module = "core");

    static void flush();

private:
    struct Impl;
    static std::unique_ptr<Impl> impl_;
    static bool initialized_;
};

}  // namespace trebuchet

#ifdef USE_SPDLOG

#include <sstream>

#define LOG_TRACE(module, ...) do { std::ostringstream _oss; _oss << __VA_ARGS__; ::trebuchet::Logger::log(::trebuchet::LogLevel::TRACE, _oss.str(), module); } while(0)
#define LOG_DEBUG(module, ...) do { std::ostringstream _oss; _oss << __VA_ARGS__; ::trebuchet::Logger::log(::trebuchet::LogLevel::DEBUG, _oss.str(), module); } while(0)
#define LOG_INFO(module, ...)  do { std::ostringstream _oss; _oss << __VA_ARGS__; ::trebuchet::Logger::log(::trebuchet::LogLevel::INFO,  _oss.str(), module); } while(0)
#define LOG_WARN(module, ...)  do { std::ostringstream _oss; _oss << __VA_ARGS__; ::trebuchet::Logger::log(::trebuchet::LogLevel::WARN,  _oss.str(), module); } while(0)
#define LOG_ERROR(module, ...) do { std::ostringstream _oss; _oss << __VA_ARGS__; ::trebuchet::Logger::log(::trebuchet::LogLevel::ERROR, _oss.str(), module); } while(0)
#define LOG_CRITICAL(module, ...) do { std::ostringstream _oss; _oss << __VA_ARGS__; ::trebuchet::Logger::log(::trebuchet::LogLevel::CRITICAL, _oss.str(), module); } while(0)

#else

#include <iostream>
#include <sstream>
#include <mutex>

#define LOG_TRACE(module, ...) do { std::ostringstream _oss; _oss << "[TRACE][" << module << "] " << __VA_ARGS__ << std::endl; std::cerr << _oss.str(); } while(0)
#define LOG_DEBUG(module, ...) do { std::ostringstream _oss; _oss << "[DEBUG][" << module << "] " << __VA_ARGS__ << std::endl; std::cerr << _oss.str(); } while(0)
#define LOG_INFO(module, ...)  do { std::ostringstream _oss; _oss << "[INFO ][" << module << "] " << __VA_ARGS__ << std::endl; std::cerr << _oss.str(); } while(0)
#define LOG_WARN(module, ...)  do { std::ostringstream _oss; _oss << "[WARN ][" << module << "] " << __VA_ARGS__ << std::endl; std::cerr << _oss.str(); } while(0)
#define LOG_ERROR(module, ...) do { std::ostringstream _oss; _oss << "[ERROR][" << module << "] " << __VA_ARGS__ << std::endl; std::cerr << _oss.str(); } while(0)
#define LOG_CRITICAL(module, ...) do { std::ostringstream _oss; _oss << "[CRIT ][" << module << "] " << __VA_ARGS__ << std::endl; std::cerr << _oss.str(); } while(0)

#endif

#endif
