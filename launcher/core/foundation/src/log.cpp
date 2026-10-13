#include "reboot/foundation/log.hpp"

#include <algorithm>
#include <array>
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

#include "reboot/foundation/secret.hpp"
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

constexpr std::size_t kCategoryCount = static_cast<std::size_t>(LogCategory::Ui) + 1;
using DropCounts = std::array<u64, kCategoryCount>;

// A category decoded from another process may be out of range.
std::size_t slot_of(LogCategory category) {
    const auto index = static_cast<std::size_t>(category);
    return index < kCategoryCount ? index : 0;
}

void add_drop_notices(std::vector<LogRecord>& batch, const DropCounts& lost) {
    std::vector<LogRecord> notices;
    for (std::size_t i = 0; i < lost.size(); ++i) {
        if (lost[i] == 0) continue;
        notices.push_back(LogRecord{std::chrono::system_clock::now(), LogLevel::Warn, LogCategory::Engine, std::nullopt,
                                    std::format("[diag] {} dropped ({})", lost[i],
                                                log_category_name(static_cast<LogCategory>(i)))});
    }
    batch.insert(batch.begin(), std::make_move_iterator(notices.begin()), std::make_move_iterator(notices.end()));
}

struct LoggerState;

class LogRedactor final : public Redactor {
public:
    explicit LogRedactor(LoggerState& state) : state_(state) {}

    void remove_secret(std::span<const u8> value) override;
    void remove_now(std::span<const u8> value) { Redactor::remove_secret(value); }

private:
    LoggerState& state_;
};

struct LoggerState {
    // Applied once no ticket of `epoch` or older is held and the writer has passed record `after`.
    struct Removal {
        u64 after = 0;
        u64 epoch = 0;
        SecretBytes value;
    };

    std::mutex mutex;
    std::condition_variable wake;
    std::condition_variable flushed;
    std::condition_variable drained;
    std::deque<LogRecord> queue;
    std::size_t queue_bytes = 0;
    std::size_t byte_budget = 0;
    DropCounts dropped{};
    u64 flush_requested = 0;
    u64 flush_done = 0;
    // Records ever queued, and how many of them the sinks have been given.
    u64 enqueued = 0;
    u64 written = 0;
    std::vector<Removal> removals;
    // Held tickets as (epoch, count), oldest first. Parking a removal starts a new epoch, so later
    // tickets never hold it back.
    u64 epoch = 0;
    std::vector<std::pair<u64, u64>> tickets;
    bool started = false;
    bool stopping = false;
    std::thread writer;

    // Only the writer thread uses the sinks; add_sink may race it, hence a lock of their own.
    std::mutex sinks_mutex;
    std::vector<std::unique_ptr<LogSink>> sinks;

    std::atomic<bool> running{false};
    std::atomic<LogLevel> minimum{LogLevel::Info};
    LogRedactor redactor{*this};

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
        // Sinks hold references to their owner's objects, which do not outlive the shutdown.
        std::vector<std::unique_ptr<LogSink>> released;
        {
            const std::lock_guard lock(sinks_mutex);
            released.swap(sinks);
        }
        released.clear();
        const std::lock_guard lock(mutex);
        started = false;
    }

    // With `mutex` held.
    void remove_secret(std::span<const u8> value) {
        if (!started || (written == enqueued && tickets.empty())) return redactor.remove_now(value);
        removals.push_back(Removal{enqueued, epoch++, SecretBytes(std::vector<u8>(value.begin(), value.end()))});
    }

    // With `mutex` held.
    u64 take_ticket() {
        if (tickets.empty() || tickets.back().first != epoch) tickets.emplace_back(epoch, 0);
        ++tickets.back().second;
        return epoch;
    }

    // With `mutex` held, after the ticket's record (if any) is queued.
    void release_ticket(u64 held, bool queued) {
        const auto it = std::ranges::find(tickets, held, &std::pair<u64, u64>::first);
        if (it != tickets.end() && --it->second == 0) tickets.erase(it);
        if (queued)
            for (Removal& removal : removals)
                if (removal.epoch >= held) removal.after = std::max(removal.after, enqueued);
        apply_removals();
    }

    // With `mutex` held.
    void apply_removals() {
        std::erase_if(removals, [this](const Removal& removal) {
            if (removal.after > written || (!tickets.empty() && tickets.front().first <= removal.epoch)) return false;
            redactor.remove_now(removal.value.reveal());
            return true;
        });
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
            const DropCounts lost = std::exchange(dropped, DropCounts{});
            const u64 batch_end = enqueued;
            const u64 flush_target = flush_requested;
            const bool exit = stopping;
            lock.unlock();
            drained.notify_all();

            add_drop_notices(batch, lost);
            if (!batch.empty()) write_batch(batch);
            if (flush_target != flush_done) flush_sinks();

            lock.lock();
            written = batch_end;
            apply_removals();
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

void LogRedactor::remove_secret(std::span<const u8> value) {
    const std::lock_guard lock(state_.mutex);
    state_.remove_secret(value);
}

}  // namespace

std::string_view log_category_name(LogCategory category) noexcept {
    static constexpr std::array<std::string_view, kCategoryCount> kNames{
        "engine",  "ipc",         "net",  "storage", "builds", "play",   "host",
        "backend", "game_output", "wine", "browser", "update", "client", "ui"};
    const auto index = static_cast<std::size_t>(category);
    return index < kNames.size() ? kNames[index] : "?";
}

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

LogTicket::LogTicket(LogTicket&& other) noexcept : epoch_(other.epoch_), held_(std::exchange(other.held_, false)) {}

LogTicket& LogTicket::operator=(LogTicket&& other) noexcept {
    if (this != &other) {
        LogTicket released = std::move(*this);
        epoch_ = other.epoch_;
        held_ = std::exchange(other.held_, false);
    }
    return *this;
}

LogTicket::~LogTicket() {
    if (!held_) return;
    LoggerState& s = state();
    const std::lock_guard lock(s.mutex);
    s.release_ticket(epoch_, false);
}

LogTicket Logger::begin() {
    LoggerState& s = state();
    const std::lock_guard lock(s.mutex);
    if (!s.started || s.stopping) return {};
    return LogTicket(s.take_ticket());
}

void Logger::set_level(LogLevel minimum) { state().minimum.store(minimum); }

bool Logger::enabled(LogLevel level) noexcept {
    const LoggerState& s = state();
    return s.running.load(std::memory_order_relaxed) && level >= s.minimum.load(std::memory_order_relaxed);
}

void Logger::write(LogLevel level, LogCategory category, std::optional<SessionId> session, std::string text) {
    write(LogTicket{}, level, category, session, std::move(text));
}

void Logger::write(LogTicket ticket, LogLevel level, LogCategory category, std::optional<SessionId> session,
                   std::string text) {
    LoggerState& s = state();
    LogRecord record{std::chrono::system_clock::now(), level, category, session, std::move(text)};
    const std::size_t bytes = cost(record);
    {
        std::unique_lock lock(s.mutex);
        const auto release = [&](bool queued) {
            if (ticket.held_) s.release_ticket(ticket.epoch_, queued);
            ticket.held_ = false;
        };
        const auto drop = [&](LogCategory counted) {
            ++s.dropped[slot_of(counted)];
            release(false);
        };
        if (!s.started || s.stopping) return release(false);
        // Taken before any wait below, so a removal made meanwhile still covers this record.
        if (!ticket.held_) ticket = LogTicket(s.take_ticket());
        if (s.queue_bytes + bytes > s.byte_budget) {
            if (droppable(record)) return drop(record.category);
            for (auto it = s.queue.begin(); it != s.queue.end() && s.queue_bytes + bytes > s.byte_budget;) {
                if (!droppable(*it)) {
                    ++it;
                    continue;
                }
                s.queue_bytes -= cost(*it);
                ++s.dropped[slot_of(it->category)];
                it = s.queue.erase(it);
            }
            if (s.queue_bytes + bytes > s.byte_budget && level < LogLevel::Warn) return drop(category);
            if (!on_writer(s))
                s.drained.wait(lock, [&] {
                    return s.stopping || s.queue.empty() || s.queue_bytes + bytes <= s.byte_budget * kHardCapBudgets;
                });
        }
        s.queue_bytes += bytes;
        s.queue.push_back(std::move(record));
        ++s.enqueued;
        release(true);
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
                add_drop_notices(batch, std::exchange(s.dropped, DropCounts{}));
                // Under the lock: the writer's next batch counts these as written and may lift their masks.
                for (LogRecord& record : batch) record.text = s.redactor.apply(record.text);
            }
        }
        s.drained.notify_all();
        batch.push_back(LogRecord{std::chrono::system_clock::now(), LogLevel::Error, category, std::nullopt,
                                  s.redactor.apply(text)});
        // The writer may have died inside a sink, still holding the sinks' lock.
        if (on_writer(s)) return;
        std::unique_lock sinks_lock(s.sinks_mutex, std::defer_lock);
        if (!lock_for_terminate(sinks_lock)) return;
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
