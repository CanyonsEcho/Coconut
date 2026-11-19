#pragma once

#include <string>
#include <string_view>
#include <vector>
#include <mutex>
#include <functional>
#include <cstdint>

namespace coconut {

enum class LogLevel {
    Trace,
    Debug,
    Info,
    Warn,
    Error
};

struct LogEntry {
    std::uint64_t sequence = 0;
    std::string timestamp;
    LogLevel level = LogLevel::Info;
    std::string category;
    std::string message;
};

class Logger {
public:
    using Listener = std::function<void(const LogEntry&)>;

    static Logger& Instance();

    void Log(LogLevel level, std::string_view category, std::string_view message);
    void Trace(std::string_view category, std::string_view message) { Log(LogLevel::Trace, category, message); }
    void Debug(std::string_view category, std::string_view message) { Log(LogLevel::Debug, category, message); }
    void Info(std::string_view category, std::string_view message)  { Log(LogLevel::Info, category, message); }
    void Warn(std::string_view category, std::string_view message)  { Log(LogLevel::Warn, category, message); }
    void Error(std::string_view category, std::string_view message) { Log(LogLevel::Error, category, message); }

    std::vector<LogEntry> Snapshot(size_t maxCount = 500) const;
    void Clear();

    void SetMinLevel(LogLevel level);
    void SetConsoleEcho(bool enabled);
    void AddListener(Listener listener);

    static std::string_view LevelName(LogLevel level);

private:
    Logger() = default;

    mutable std::mutex mutex_;
    std::vector<LogEntry> entries_;
    std::vector<Listener> listeners_;
    std::uint64_t nextSeq_ = 1;
    LogLevel minLevel_ = LogLevel::Debug;
    bool consoleEcho_ = true;
    static constexpr size_t kMaxEntries = 4096;
};

#define COCO_LOG_INFO(cat, msg)  ::coconut::Logger::Instance().Info(cat, msg)
#define COCO_LOG_WARN(cat, msg)  ::coconut::Logger::Instance().Warn(cat, msg)
#define COCO_LOG_ERROR(cat, msg) ::coconut::Logger::Instance().Error(cat, msg)
#define COCO_LOG_DEBUG(cat, msg) ::coconut::Logger::Instance().Debug(cat, msg)

} // namespace coconut
