#pragma once

#include <chrono>
#include <cstddef>
#include <vector>

#include "reboot/base/function.hpp"
#include "reboot/browser/join_target.hpp"

namespace rb {
class EventBus;
class IClock;
}  // namespace rb

namespace rb::browser {

// A server or an address Play joined.
struct JoinHistoryEntry {
    JoinTarget target;
    std::chrono::system_clock::time_point last_joined;

    bool operator==(const JoinHistoryEntry&) const = default;
};

// Payload of EventKind::JoinHistoryChanged: the whole history, newest first.
struct JoinHistoryChanged {
    std::vector<JoinHistoryEntry> entries;

    [[nodiscard]] std::size_t approx_bytes() const noexcept { return entries.size() * sizeof(JoinHistoryEntry); }
};

// Capabilities: none assigned (owner decision: an engine-side join history).
// Strand-only. What Play joined, newest first: one entry per server id, or per host (without
// regard to ASCII case) and port for a typed address, so a repeat moves to the top with its newer
// name; only the newest kMaxEntries stay. Every change publishes JoinHistoryChanged and hands the
// list to `persist`.
class JoinHistory {
public:
    static constexpr std::size_t kMaxEntries = 50;
    using Persist = UniqueFunction<void(const std::vector<JoinHistoryEntry>&)>;

    // `loaded` is put in the same order and bounds, without a persist or an event.
    JoinHistory(const IClock& clock, EventBus& events, std::vector<JoinHistoryEntry> loaded, Persist persist);

    [[nodiscard]] const std::vector<JoinHistoryEntry>& entries() const noexcept { return entries_; }

    void record(JoinTarget target);
    // False when no entry is that server or address.
    bool remove(const JoinTarget& target);
    void clear();

private:
    void changed();

    const IClock& clock_;
    EventBus& events_;
    Persist persist_;
    std::vector<JoinHistoryEntry> entries_;
};

}  // namespace rb::browser
