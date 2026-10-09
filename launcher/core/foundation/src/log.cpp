#include "reboot/foundation/log.hpp"

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <deque>
#include <functional>
#include <map>
#include <mutex>
#include <shared_mutex>
#include <thread>
#include <utility>
#include <vector>

#include "reboot/foundation/text.hpp"

namespace rb {

namespace {

constexpr std::string_view kMask = "***";
constexpr std::string_view kPasswordKey = "-AUTH_PASSWORD=";
constexpr std::size_t kMinSecretBytes = 4;
// Warn and above never drop; past this many budgets their producers wait for the writer.
constexpr std::size_t kHardCapBudgets = 2;

using Span = std::pair<std::size_t, std::size_t>;

void password_spans(std::string_view text, std::vector<Span>& spans) {
    for (std::size_t at = 0; at + kPasswordKey.size() <= text.size(); ++at) {
        if (!iequals_ascii(text.substr(at, kPasswordKey.size()), kPasswordKey)) continue;
        std::size_t start = at + kPasswordKey.size();
        std::size_t end = start;
        if (start < text.size() && text[start] == '"') {
            ++start;
            end = std::min(text.find('"', start), text.size());
        } else {
            while (end < text.size() && text[end] != ' ' && text[end] != '\t' && text[end] != '\r' && text[end] != '\n') ++end;
        }
        if (end > start) spans.emplace_back(start, end);
        at = end - 1;
    }
}

// Warn and above never drop, whatever their category.
bool droppable(const LogRecord& record) {
    if (record.level >= LogLevel::Warn) return false;
    return record.category == LogCategory::GameOutput || record.category == LogCategory::Wine ||
           record.level <= LogLevel::Debug;
}

std::size_t cost(const LogRecord& record) { return sizeof(LogRecord) + record.text.size(); }

struct LoggerState {
    std::mutex mutex;
    std::condition_variable wake;
    std::condition_variable flushed;
    std::condition_variable drained;
    std::deque<LogRecord> queue;
    std::size_t queue_bytes = 0;
    std::size_t byte_budget = 0;
    u64 dropped = 0;
    u64 flush_requested = 0;
    u64 flush_done = 0;
    bool started = false;
    bool stopping = false;
    std::thread writer;

    // Only the writer thread uses the sinks; add_sink may race it, hence a lock of their own.
    std::mutex sinks_mutex;
    std::vector<std::unique_ptr<LogSink>> sinks;

    std::atomic<bool> running{false};
    std::atomic<LogLevel> minimum{LogLevel::Info};
    Redactor redactor;

    ~LoggerState() { stop(); }

    void stop() {
        {
            const std::lock_guard lock(mutex);
            if (!started || stopping) return;
            stopping = true;
            running.store(false);
        }
        wake.notify_all();
        writer.join();
        flush_sinks();
        const std::lock_guard lock(mutex);
        started = false;
    }

    void write_batch(std::vector<LogRecord>& batch) {
        for (LogRecord& record : batch) record.text = redactor.apply(record.text);
        const std::lock_guard lock(sinks_mutex);
        for (const auto& sink : sinks) {
            try {
                sink->write(batch);
            } catch (...) {
                // A failing sink must not stop the others; nothing is left to report it to.
            }
        }
    }

    void flush_sinks() {
        const std::lock_guard lock(sinks_mutex);
        for (const auto& sink : sinks) {
            try {
                sink->flush();
            } catch (...) {
            }
        }
    }

    void run_writer() {
        std::unique_lock lock(mutex);
        while (true) {
            wake.wait(lock, [this] { return stopping || !queue.empty() || flush_requested != flush_done; });
            std::vector<LogRecord> batch(std::make_move_iterator(queue.begin()), std::make_move_iterator(queue.end()));
            queue.clear();
            queue_bytes = 0;
            const u64 lost = std::exchange(dropped, 0);
            const u64 flush_target = flush_requested;
            const bool exit = stopping;
            lock.unlock();
            drained.notify_all();

            if (lost != 0)
                batch.insert(batch.begin(), LogRecord{std::chrono::system_clock::now(), LogLevel::Warn, LogCategory::Engine,
                                                      std::nullopt, std::format("{} log records were dropped", lost)});
            if (!batch.empty()) write_batch(batch);
            if (flush_target != flush_done) flush_sinks();

            lock.lock();
            flush_done = flush_target;
            flushed.notify_all();
            if (exit && queue.empty()) return;
        }
    }
};

LoggerState& state() {
    static LoggerState instance;
    return instance;
}

// A sink that logs runs on the writer, which must never wait for itself.
bool on_writer(const LoggerState& s) { return s.writer.get_id() == std::this_thread::get_id(); }

}  // namespace

struct Redactor::Impl {
    mutable std::shared_mutex mutex;
    std::map<std::string, std::size_t, std::less<>> secrets;
};

Redactor::Redactor() : impl_(std::make_unique<Impl>()) {}

Redactor::~Redactor() = default;

void Redactor::add_secret(std::span<const u8> value) {
    if (value.size() < kMinSecretBytes) return;
    const std::unique_lock lock(impl_->mutex);
    ++impl_->secrets[std::string(value.begin(), value.end())];
}

void Redactor::remove_secret(std::span<const u8> value) {
    if (value.size() < kMinSecretBytes) return;
    const std::unique_lock lock(impl_->mutex);
    const auto it = impl_->secrets.find(std::string(value.begin(), value.end()));
    if (it != impl_->secrets.end() && --it->second == 0) impl_->secrets.erase(it);
}

std::string Redactor::apply(std::string_view text) const {
    // Matches are found on the original text and merged, so overlapping secrets leave no
    // unmasked part and a mask never forms a new match.
    std::vector<Span> spans;
    {
        const std::shared_lock lock(impl_->mutex);
        for (const auto& entry : impl_->secrets) {
            const std::string& secret = entry.first;
            for (std::size_t at = text.find(secret); at != std::string_view::npos; at = text.find(secret, at + 1))
                spans.emplace_back(at, at + secret.size());
        }
    }
    password_spans(text, spans);
    if (spans.empty()) return std::string(text);

    std::ranges::sort(spans);
    std::string out;
    out.reserve(text.size());
    std::size_t copied = 0;
    for (std::size_t i = 0; i < spans.size();) {
        const std::size_t begin = spans[i].first;
        std::size_t end = spans[i].second;
        for (++i; i < spans.size() && spans[i].first < end; ++i) end = std::max(end, spans[i].second);
        out.append(text.substr(copied, begin - copied));
        out.append(kMask);
        copied = end;
    }
    out.append(text.substr(copied));
    return out;
}

void Logger::install(std::size_t byte_budget) {
    LoggerState& s = state();
    const std::lock_guard lock(s.mutex);
    s.byte_budget = byte_budget;
    if (s.started) return;
    s.started = true;
    s.stopping = false;
    s.writer = std::thread([&s] {
        try {
            s.run_writer();
        } catch (...) {
            // The writer is the last resort for reporting; it can only stop.
        }
    });
    s.running.store(true);
}

void Logger::add_sink(std::unique_ptr<LogSink> sink) {
    LoggerState& s = state();
    const std::lock_guard lock(s.sinks_mutex);
    s.sinks.push_back(std::move(sink));
}

Redactor& Logger::redactor() { return state().redactor; }

void Logger::set_level(LogLevel minimum) { state().minimum.store(minimum); }

bool Logger::enabled(LogLevel level) noexcept {
    const LoggerState& s = state();
    return s.running.load(std::memory_order_relaxed) && level >= s.minimum.load(std::memory_order_relaxed);
}

void Logger::write(LogLevel level, LogCategory category, std::optional<SessionId> session, std::string text) {
    LoggerState& s = state();
    LogRecord record{std::chrono::system_clock::now(), level, category, session, std::move(text)};
    const std::size_t bytes = cost(record);
    {
        std::unique_lock lock(s.mutex);
        if (!s.started || s.stopping) return;
        if (s.queue_bytes + bytes > s.byte_budget) {
            if (droppable(record)) {
                ++s.dropped;
                return;
            }
            for (auto it = s.queue.begin(); it != s.queue.end() && s.queue_bytes + bytes > s.byte_budget;) {
                if (!droppable(*it)) {
                    ++it;
                    continue;
                }
                s.queue_bytes -= cost(*it);
                it = s.queue.erase(it);
                ++s.dropped;
            }
            if (s.queue_bytes + bytes > s.byte_budget && level < LogLevel::Warn) {
                ++s.dropped;
                return;
            }
            if (!on_writer(s))
                s.drained.wait(lock, [&] {
                    return s.stopping || s.queue.empty() || s.queue_bytes + bytes <= s.byte_budget * kHardCapBudgets;
                });
        }
        s.queue_bytes += bytes;
        s.queue.push_back(std::move(record));
    }
    s.wake.notify_one();
}

void Logger::flush() {
    LoggerState& s = state();
    std::unique_lock lock(s.mutex);
    if (!s.started || s.stopping || on_writer(s)) return;
    const u64 ticket = ++s.flush_requested;
    s.wake.notify_one();
    // The writer settles every requested flush before it exits, so this returns even during shutdown.
    s.flushed.wait(lock, [&] { return s.flush_done >= ticket; });
}

void Logger::shutdown() { state().stop(); }

namespace {

// Bounded, so a writer stuck inside a sink cannot hold the terminate path forever.
template <class Mutex>
bool lock_for_terminate(std::unique_lock<Mutex>& lock) {
    for (int attempt = 0; attempt < 100; ++attempt) {
        if (lock.try_lock()) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds{10});
    }
    return false;
}

}  // namespace

void Logger::drain_for_terminate(LogCategory category, std::string text) noexcept {
    try {
        LoggerState& s = state();
        std::vector<LogRecord> batch;
        {
            std::unique_lock lock(s.mutex, std::defer_lock);
            if (lock_for_terminate(lock)) {
                batch.assign(std::make_move_iterator(s.queue.begin()), std::make_move_iterator(s.queue.end()));
                s.queue.clear();
                s.queue_bytes = 0;
                if (const u64 lost = std::exchange(s.dropped, 0); lost != 0)
                    batch.insert(batch.begin(), LogRecord{std::chrono::system_clock::now(), LogLevel::Warn, LogCategory::Engine,
                                                          std::nullopt, std::format("{} log records were dropped", lost)});
            }
        }
        s.drained.notify_all();
        batch.push_back(LogRecord{std::chrono::system_clock::now(), LogLevel::Error, category, std::nullopt, std::move(text)});
        // The writer may have died inside a sink, still holding the sinks' lock.
        if (on_writer(s)) return;
        std::unique_lock sinks_lock(s.sinks_mutex, std::defer_lock);
        if (!lock_for_terminate(sinks_lock)) return;
        for (LogRecord& record : batch) record.text = s.redactor.apply(record.text);
        for (const auto& sink : s.sinks) {
            try {
                sink->write(batch);
                sink->flush();
            } catch (...) {
            }
        }
    } catch (...) {
    }
}

}  // namespace rb
