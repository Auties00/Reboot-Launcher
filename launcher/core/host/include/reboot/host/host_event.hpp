#pragma once

#include <chrono>
#include <optional>
#include <variant>
#include <vector>

#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/events.hpp"
#include "reboot/foundation/types.hpp"
#include "reboot/gameserver/game_server_event.hpp"
#include "reboot/host/host_phase.hpp"
#include "reboot/host/match_end_policy.hpp"
#include "reboot/host/port_block.hpp"

namespace reboot::host {

// EventKind::HostPhaseChanged. `reason` explains Failed, LiveUnpublished and a Restarting that
// fell back to a respawn.
struct HostPhaseChanged {
    SessionId session;
    HostPhase phase = HostPhase::Preparing;
    std::optional<Diagnostic> reason;
};

// EventKind::HostListening, after every successful listen, so a respawn on a new block shows.
struct HostListening {
    SessionId session;
    PortBlock block;
    std::vector<gameserver::BoundSocket> bound;
};

// Ended is the engine's view after MatchEnded; the server itself goes back to Lobby.
enum class HostMatchState : u8 { Lobby, Warmup, InProgress, Ending, Ended };

// EventKind::MatchEvent. On Ended, `action` and `fires_at` say what the match-end timer will do;
// a cancelled timer publishes Ended again without them.
struct MatchEvent {
    SessionId session;
    HostMatchState state = HostMatchState::Lobby;
    std::optional<gameserver::MatchEnded> result;
    std::optional<MatchEndAction> action;
    std::optional<std::chrono::system_clock::time_point> fires_at;
};

enum class PlayerChange : u8 { Joined, Left };

// EventKind::PlayerEvent. The account id is what the client claimed.
struct PlayerEvent {
    SessionId session;
    PlayerChange change = PlayerChange::Joined;
    gameserver::Player player;
    u32 player_count = 0;
};

// Port mapping, reachability and publish state are published by net and publish.
using HostEvent = std::variant<HostPhaseChanged, HostListening, MatchEvent, PlayerEvent>;

[[nodiscard]] constexpr EventKind event_kind(const HostEvent& event) noexcept {
    switch (event.index()) {
        case 0: return EventKind::HostPhaseChanged;
        case 1: return EventKind::HostListening;
        case 2: return EventKind::MatchEvent;
        default: return EventKind::PlayerEvent;
    }
}

[[nodiscard]] constexpr const SessionId& event_session(const HostEvent& event) noexcept {
    return std::visit([](const auto& payload) -> const SessionId& { return payload.session; }, event);
}

}  // namespace reboot::host
