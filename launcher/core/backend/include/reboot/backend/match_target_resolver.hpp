#pragma once

#include <optional>
#include <string>

#include "reboot/foundation/net_types.hpp"

namespace reboot::backend {

struct MatchTargetQuery {
    std::string account_id;
    std::string playlist;
};

// No endpoint: nothing is recorded for the account.
struct ResolvedMatchTarget {
    std::optional<HostPort> endpoint;
    std::optional<Port> beacon_port;
};

// Capabilities: matchmaking-networking.matchmaker-address-sync.
// Installed by the engine on BackendProcess; strand-only. Play records each session's target at
// launch and the backend pulls it per matchmaking request, so a join rewrites no config file and
// needs no backend restart.
class IMatchTargetResolver {
public:
    virtual ~IMatchTargetResolver() = default;

    [[nodiscard]] virtual ResolvedMatchTarget resolve(const MatchTargetQuery& query) = 0;
};

}  // namespace reboot::backend
