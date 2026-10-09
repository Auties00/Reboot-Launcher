#pragma once

#include <optional>
#include <vector>

#include "reboot/gameserver/game_server_event.hpp"
#include "reboot/host/host_event.hpp"
#include "reboot/net/port_mapping.hpp"
#include "reboot/publish/publish_state.hpp"
#include "reboot/publish/reachability_changed.hpp"

namespace rb::host {

// Host.status: what the session's events said last, so a UI that attaches mid-session needs no replay.
struct HostSnapshot {
    HostPhaseChanged phase;
    // Absent before Listening and during a respawn.
    std::optional<HostListening> listening;
    // Absent while the session is not published.
    std::optional<publish::PublishState> publish;
    std::optional<publish::ReachabilityChanged> reachability;
    std::vector<net::PortMapping> mappings;
    MatchEvent match;
    std::vector<gameserver::Player> players;
};

}  // namespace rb::host
