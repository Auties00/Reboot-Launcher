#pragma once

#include <optional>
#include <string>

#include "reboot/backend/match_target_resolver.hpp"
#include "reboot/foundation/net_types.hpp"
#include "reboot/foundation/types.hpp"

namespace rb::play {

struct MatchTargetEntry {
    // The play session's account, as -AUTH_LOGIN's local part carries it.
    std::string account_id;
    HostPort endpoint;
    std::optional<Port> beacon_port;

    bool operator==(const MatchTargetEntry&) const = default;
};

// Covers game-launch.orchestration.
// Strand-only. The engine installs it as the BackendProcess resolver; play publishes a session's
// target before launch and withdraws it at the end. One entry, since one play session runs at a
// time: MatchTargetQuery names only the account, which every play session shares.
class MatchTargets final : public backend::IMatchTargetResolver {
public:
    // Replaces any entry.
    void publish(SessionId session, MatchTargetEntry entry);
    // Clears the entry if `session` published it.
    void withdraw(SessionId session);

    [[nodiscard]] std::optional<MatchTargetEntry> find(SessionId session) const;

    // The entry when its account matches; the playlist is not used.
    [[nodiscard]] backend::ResolvedMatchTarget resolve(const backend::MatchTargetQuery& query) override;

private:
    struct Published {
        SessionId session;
        MatchTargetEntry entry;
    };

    std::optional<Published> published_;
};

}  // namespace rb::play
