#include "logger.h"

#include <iostream>
#include <filesystem>
#include <mutex>

#ifdef USE_SPDLOG
#include "spdlog/spdlog.h"
#include "spdlog/sinks/basic_file_sink.h"
#include "spdlog/sinks/stdout_color_sinks.h"
#include "spdlog/sinks/rotating_file_sink.h"
#endif

namespace trebuchet {

struct Logger::Impl {
#ifdef USE_SPDLOG
    std::shared_ptr<spdlog::logger> logger;
#endif
    std::mutex log_mutex;
};

std::unique_ptr<Logger::Impl> Logger::impl_ = nullptr;
bool Logger::initialized_ = false;

static spdlog::level::level_enum mapLevel(LogLevel lvl) {
#ifdef USE_SPDLOG
    switch (lvl) {
        case LogLevel::TRACE: return spdlog::level::trace;
        case LogLevel::DEBUG: return spdlog::level::debug;
        case LogLevel::INFO:  return spdlog::level::info;
        case LogLevel::WARN:  return spdlog::level::warn;
        case LogLevel::ERROR: return spdlog::level::err;
        case LogLevel::CRITICAL: return spdlog::level::critical;
    }
    return spdlog::level::info;
#else
    return spdlog::level::info;
#endif
}

void Logger::init(const std::string& log_dir,
                  const std::string& file_name,
                  LogLevel console_level,
                  LogLevel file_level) {
    if (initialized_) return;

    impl_ = std::make_unique<Impl>();
    initialized_ = true;

#ifdef USE_SPDLOG
    try {
        if (!std::filesystem::exists(log_dir)) {
            std::filesystem::create_directories(log_dir);
        }

        auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
        console_sink->set_level(mapLevel(console_level));
        console_sink->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] [%n] [%t] %v");

        auto file_sink = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
            log_dir + "/" + file_name,
            10 * 1024 * 1024,
            5
        );
        file_sink->set_level(mapLevel(file_level));
        file_sink->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%l] [%n] [%t] %v");

        std::vector<spdlog::sink_ptr> sinks{ console_sink, file_sink };
        impl_->logger = std::make_shared<spdlog::logger>("trebuchet", sinks.begin(), sinks.end());
        impl_->logger->set_level(spdlog::level::trace);
        impl_->logger->flush_on(spdlog::level::warn);

        spdlog::register_logger(impl_->logger);
        spdlog::set_default_logger(impl_->logger);
    } catch (const spdlog::spdlog_ex& ex) {
        std::cerr << "[logger] spdlog init failed: " << ex.what() << std::endl;
    }
#endif
}

void Logger::setLevel(LogLevel level) {
#ifdef USE_SPDLOG
    if (impl_ && impl_->logger) {
        impl_->logger->set_level(mapLevel(level));
    }
#endif
}

void Logger::log(LogLevel level, const std::string& message, const std::string& module) {
    if (!initialized_) init();

#ifdef USE_SPDLOG
    if (impl_ && impl_->logger) {
        std::lock_guard<std::mutex> lock(impl_->log_mutex);
        switch (level) {
            case LogLevel::TRACE:    impl_->logger->trace("[{}] {}", module, message); break;
            case LogLevel::DEBUG:    impl_->logger->debug("[{}] {}", module, message); break;
            case LogLevel::INFO:     impl_->logger->info("[{}] {}", module, message); break;
            case LogLevel::WARN:     impl_->logger->warn("[{}] {}", module, message); break;
            case LogLevel::ERROR:    impl_->logger->error("[{}] {}", module, message); break;
            case LogLevel::CRITICAL: impl_->logger->critical("[{}] {}", module, message); break;
        }
    } else {
        std::cerr << "[log][" << module << "] " << message << std::endl;
    }
#else
    std::lock_guard<std::mutex> lock(impl_->log_mutex);
    const char* lvl_str = "INFO";
    switch (level) {
        case LogLevel::TRACE:    lvl_str = "TRACE"; break;
        case LogLevel::DEBUG:    lvl_str = "DEBUG"; break;
        case LogLevel::INFO:     lvl_str = "INFO "; break;
        case LogLevel::WARN:     lvl_str = "WARN "; break;
        case LogLevel::ERROR:    lvl_str = "ERROR"; break;
        case LogLevel::CRITICAL: lvl_str = "CRIT "; break;
    }
    std::cerr << "[" << lvl_str << "][" << module << "] " << message << std::endl;
#endif
}

void Logger::flush() {
#ifdef USE_SPDLOG
    if (impl_ && impl_->logger) {
        impl_->logger->flush();
    }
#endif
}

}  // namespace trebuchet
