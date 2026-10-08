#pragma once

#include <compare>
#include <cstddef>
#include <memory>
#include <span>
#include <vector>

#include "reboot/foundation/function.hpp"
#include "reboot/foundation/log.hpp"
#include "reboot/foundation/types.hpp"
#include "reboot/logging/log_filter.hpp"

namespace reboot::logging {

inline constexpr std::size_t kLogRingBytes = 4u << 20;
inline constexpr std::size_t kLogReadMaxBytes = 1u << 20;

// Sequence numbers start at 1 in each engine run, so the default cursor reads from the oldest retained entry.
struct LogCursor {
    u64 after_seq = 0;

    constexpr auto operator<=>(const LogCursor&) const = default;
};

struct LogEntry {
    u64 seq = 0;
    LogRecord record;
};

// `next` is past every entry scanned, matched or not. `missed` counts entries evicted unread.
struct LogPage {
    std::vector<LogEntry> entries;
    LogCursor next;
    u64 missed = 0;
};

// Capabilities: logging-diagnostics.log-and-errors.
// Backs Logs.read. Thread-safe: the writer appends, the strand reads. Wine-routed records are skipped.
class LogRing final : public LogSink {
public:
    explicit LogRing(std::size_t byte_budget = kLogRingBytes);
    ~LogRing() override;
    LogRing(const LogRing&) = delete;
    LogRing& operator=(const LogRing&) = delete;

    void write(std::span<const LogRecord> records) override;
    void flush() override {}

    // Oldest first, at most `max_entries` and kLogReadMaxBytes of text, but never empty for lack of bytes.
    // A cursor past the newest entry, such as one from an earlier run, reads from end().
    [[nodiscard]] LogPage read(LogCursor cursor, const LogFilter& filter, std::size_t max_entries) const;
    // Past the newest entry: a reader starting here sees only new lines.
    [[nodiscard]] LogCursor end() const;

    // Runs on the writer thread, with no ring lock held, after each batch that added entries.
    // Thread-safe; once it returns, the previous callback is no longer running.
    void set_on_append(UniqueFunction<void()> on_append);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace reboot::logging
