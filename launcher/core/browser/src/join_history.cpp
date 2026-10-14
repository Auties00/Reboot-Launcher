#include "reboot/browser/join_history.hpp"

#include <algorithm>
#include <string>
#include <utility>

#include "reboot/foundation/clock.hpp"
#include "reboot/foundation/events.hpp"
#include "reboot/foundation/text.hpp"

namespace rb::browser {

namespace {

[[nodiscard]] bool same_place(const JoinTarget& a, const JoinTarget& b) {
    if (const auto* server = std::get_if<ServerTarget>(&a.target)) {
        const auto* other = std::get_if<ServerTarget>(&b.target);
        return other != nullptr && other->id == server->id;
    }
    const auto* other = std::get_if<AddressTarget>(&b.target);
    const HostPort& address = std::get<AddressTarget>(a.target).address;
    return other != nullptr && other->address.port == address.port && iequals_ascii(other->address.host, address.host);
}

// Newest first, one entry per place, at most kMaxEntries.
void normalize(std::vector<JoinHistoryEntry>& entries) {
    std::ranges::stable_sort(entries, [](const JoinHistoryEntry& a, const JoinHistoryEntry& b) {
        return a.last_joined > b.last_joined;
    });
    std::vector<JoinHistoryEntry> kept;
    for (JoinHistoryEntry& entry : entries) {
        if (kept.size() == JoinHistory::kMaxEntries) break;
        if (std::ranges::none_of(kept, [&](const JoinHistoryEntry& k) { return same_place(k.target, entry.target); }))
            kept.push_back(std::move(entry));
    }
    entries = std::move(kept);
}

}  // namespace

JoinHistory::JoinHistory(const IClock& clock, EventBus& events, std::vector<JoinHistoryEntry> loaded, Persist persist)
    : clock_(clock), events_(events), persist_(std::move(persist)), entries_(std::move(loaded)) {
    normalize(entries_);
}

void JoinHistory::record(JoinTarget target) {
    std::erase_if(entries_, [&](const JoinHistoryEntry& entry) { return same_place(entry.target, target); });
    entries_.insert(entries_.begin(), JoinHistoryEntry{std::move(target), clock_.system_now()});
    if (entries_.size() > kMaxEntries) entries_.resize(kMaxEntries);
    changed();
}

bool JoinHistory::remove(const JoinTarget& target) {
    if (std::erase_if(entries_, [&](const JoinHistoryEntry& entry) { return same_place(entry.target, target); }) == 0)
        return false;
    changed();
    return true;
}

void JoinHistory::clear() {
    if (entries_.empty()) return;
    entries_.clear();
    changed();
}

void JoinHistory::changed() {
    if (persist_) persist_(entries_);
    events_.publish(EventKind::JoinHistoryChanged, JoinHistoryChanged{entries_},
                    EventScope{.session = std::nullopt, .op = std::nullopt, .coalesce_key = "join_history"});
}

}  // namespace rb::browser
