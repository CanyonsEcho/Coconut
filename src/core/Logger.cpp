#include "core/Logger.h"

#include <chrono>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <sstream>

namespace coconut {

Logger& Logger::Instance() {
    static Logger instance;
    return instance;
}

std::string_view Logger::LevelName(LogLevel level) {
    switch (level) {
        case LogLevel::Trace: return "TRACE";
        case LogLevel::Debug: return "DEBUG";
        case LogLevel::Info:  return "INFO";
        case LogLevel::Warn:  return "WARN";
        case LogLevel::Error: return "ERROR";
    }
    return "INFO";
}

void Logger::Log(LogLevel level, std::string_view category, std::string_view message) {
    if (static_cast<int>(level) < static_cast<int>(minLevel_)) {
        return;
    }

    auto now = std::chrono::system_clock::now();
    std::time_t t = std::chrono::system_clock::to_time_t(now);
    std::tm tmBuf{};
#if defined(_WIN32)
    localtime_s(&tmBuf, &t);
#else
    localtime_r(&t, &tmBuf);
#endif
    std::ostringstream ts;
    ts << std::put_time(&tmBuf, "%H:%M:%S");

    LogEntry entry;
    std::vector<Listener> listenersCopy;
    bool echo = false;

    {
        std::lock_guard<std::mutex> lock(mutex_);
        entry.sequence = nextSeq_++;
        entry.timestamp = ts.str();
        entry.level = level;
        entry.category = std::string(category);
        entry.message = std::string(message);

        if (entries_.size() >= kMaxEntries) {
            entries_.erase(entries_.begin(), entries_.begin() + (kMaxEntries / 4));
        }
        entries_.push_back(entry);
        listenersCopy = listeners_;
        echo = consoleEcho_;
    }

    if (echo) {
        std::ostream& os = (level == LogLevel::Error || level == LogLevel::Warn) ? std::cerr : std::cout;
        os << '[' << entry.timestamp << "] [" << LevelName(level) << "] [" << entry.category << "] "
           << entry.message << '\n';
    }

    for (const auto& cb : listenersCopy) {
        if (cb) cb(entry);
    }
}

std::vector<LogEntry> Logger::Snapshot(size_t maxCount) const {
    std::lock_guard<std::mutex> lock(mutex_);
    if (entries_.size() <= maxCount) {
        return entries_;
    }
    return std::vector<LogEntry>(entries_.end() - static_cast<std::ptrdiff_t>(maxCount), entries_.end());
}

void Logger::Clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    entries_.clear();
}

void Logger::SetMinLevel(LogLevel level) {
    std::lock_guard<std::mutex> lock(mutex_);
    minLevel_ = level;
}

void Logger::SetConsoleEcho(bool enabled) {
    std::lock_guard<std::mutex> lock(mutex_);
    consoleEcho_ = enabled;
}

void Logger::AddListener(Listener listener) {
    std::lock_guard<std::mutex> lock(mutex_);
    listeners_.push_back(std::move(listener));
}

} // namespace coconut
