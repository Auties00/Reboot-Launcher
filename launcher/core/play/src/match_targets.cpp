#include "reboot/play/match_targets.hpp"

#include <utility>

namespace reboot::play {

void MatchTargets::publish(SessionId session, MatchTargetEntry entry) {
    published_ = Published{session, std::move(entry)};
}

void MatchTargets::withdraw(SessionId session) {
    if (published_ && published_->session == session) published_.reset();
}

std::optional<MatchTargetEntry> MatchTargets::find(SessionId session) const {
    if (published_ && published_->session == session) return published_->entry;
    return std::nullopt;
}

backend::ResolvedMatchTarget MatchTargets::resolve(const backend::MatchTargetQuery& query) {
    if (!published_ || published_->entry.account_id != query.account_id) return {};
    return backend::ResolvedMatchTarget{published_->entry.endpoint, published_->entry.beacon_port};
}

}  // namespace reboot::play
