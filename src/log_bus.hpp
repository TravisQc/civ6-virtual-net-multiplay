// 线程安全的有界日志队列。工作线程只调用 push；UI 线程用 drain 抽干。
// 对齐 Python 版 queue.Queue(maxsize=5000)：满则丢弃，避免极端流量下堆积占内存。
#pragma once

#include <deque>
#include <functional>
#include <mutex>
#include <string>
#include <utility>

namespace civ6 {

// 日志级别，与 Python 版一致：info / warn / error / debug
enum class LogLevel { Info, Warn, Error, Debug };

inline const char* to_string(LogLevel level) {
    switch (level) {
        case LogLevel::Info:  return "info";
        case LogLevel::Warn:  return "warn";
        case LogLevel::Error: return "error";
        case LogLevel::Debug: return "debug";
    }
    return "info";
}

inline LogLevel level_from_string(const std::string& s) {
    if (s == "warn")  return LogLevel::Warn;
    if (s == "error") return LogLevel::Error;
    if (s == "debug") return LogLevel::Debug;
    return LogLevel::Info;
}

struct LogEntry {
    LogLevel level;
    std::string message;
};

// 引擎通过该回调把日志送回：签名对齐 Python 的 log(level, message)。
using LogFn = std::function<void(LogLevel, const std::string&)>;

class LogBus {
public:
    explicit LogBus(std::size_t max_size = 5000) : max_size_(max_size) {}

    // 任意线程可调用。队列满则静默丢弃（对齐 Python queue.Full -> pass）。
    void push(LogLevel level, const std::string& message) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (queue_.size() >= max_size_) return;
        queue_.push_back({level, message});
    }

    // 抽干队列，交给 UI 线程消费。
    std::deque<LogEntry> drain() {
        std::lock_guard<std::mutex> lock(mutex_);
        std::deque<LogEntry> out;
        out.swap(queue_);
        return out;
    }

    // 便于当作 LogFn 传给引擎。
    LogFn as_fn() {
        return [this](LogLevel level, const std::string& msg) { this->push(level, msg); };
    }

private:
    std::size_t max_size_;
    std::mutex mutex_;
    std::deque<LogEntry> queue_;
};

}  // namespace civ6
