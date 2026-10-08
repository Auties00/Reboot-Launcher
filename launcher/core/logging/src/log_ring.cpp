#include "reboot/logging/log_ring.hpp"

#include <deque>
#include <mutex>
#include <utility>

#include "reboot/logging/wine_log_files.hpp"

namespace reboot::logging {

namespace {

[[nodiscard]] std::size_t entry_bytes(const LogEntry& entry) noexcept {
    return sizeof(LogEntry) + entry.record.text.size();
}

}  // namespace

struct LogRing::Impl {
    explicit Impl(std::size_t budget) : byte_budget(budget) {}

    const std::size_t byte_budget;
    mutable std::mutex mutex;
    std::deque<LogEntry> entries;
    std::size_t used_bytes = 0;
    u64 next_seq = 1;

    // Held while the callback runs, so set_on_append can wait it out.
    std::mutex callback_mutex;
    UniqueFunction<void()> on_append;

    // Seq of entries.front(), or next_seq when empty; every older entry was evicted.
    [[nodiscard]] u64 oldest_seq() const noexcept { return entries.empty() ? next_seq : entries.front().seq; }
};

LogRing::LogRing(std::size_t byte_budget) : impl_(std::make_unique<Impl>(byte_budget)) {}

LogRing::~LogRing() = default;

void LogRing::write(std::span<const LogRecord> records) {
    bool appended = false;
    {
        const std::lock_guard lock(impl_->mutex);
        for (const LogRecord& record : records) {
            if (routes_to_wine_log(record)) continue;
            LogEntry& entry = impl_->entries.emplace_back(LogEntry{impl_->next_seq++, record});
            impl_->used_bytes += entry_bytes(entry);
            appended = true;
        }
        while (impl_->used_bytes > impl_->byte_budget && impl_->entries.size() > 1) {
            impl_->used_bytes -= entry_bytes(impl_->entries.front());
            impl_->entries.pop_front();
        }
    }
    if (!appended) return;
    const std::lock_guard lock(impl_->callback_mutex);
    if (impl_->on_append) impl_->on_append();
}

LogPage LogRing::read(LogCursor cursor, const LogFilter& filter, std::size_t max_entries) const {
    const std::lock_guard lock(impl_->mutex);
    LogPage page;
    const u64 newest = impl_->next_seq - 1;
    if (cursor.after_seq > newest) cursor.after_seq = newest;
    page.next = cursor;

    const u64 oldest = impl_->oldest_seq();
    if (cursor.after_seq + 1 < oldest) {
        page.missed = oldest - cursor.after_seq - 1;
        cursor.after_seq = oldest - 1;
        page.next = cursor;
    }

    std::size_t text_bytes = 0;
    for (auto it = impl_->entries.begin() + static_cast<std::ptrdiff_t>(cursor.after_seq + 1 - oldest);
         it != impl_->entries.end() && page.entries.size() < max_entries; ++it) {
        if (filter.matches(it->record)) {
            text_bytes += it->record.text.size();
            if (text_bytes > kLogReadMaxBytes && !page.entries.empty()) break;
            page.entries.push_back(*it);
        }
        page.next.after_seq = it->seq;
    }
    return page;
}

LogCursor LogRing::end() const {
    const std::lock_guard lock(impl_->mutex);
    return LogCursor{impl_->next_seq - 1};
}

void LogRing::set_on_append(UniqueFunction<void()> on_append) {
    UniqueFunction<void()> previous;
    {
        const std::lock_guard lock(impl_->callback_mutex);
        previous = std::exchange(impl_->on_append, std::move(on_append));
    }
}

}  // namespace reboot::logging
