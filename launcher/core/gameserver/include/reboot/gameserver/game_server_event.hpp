#pragma once

#include <optional>
#include <string>
#include <variant>
#include <vector>

#include "reboot/contracts/game_server.hpp"
#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/net_types.hpp"
#include "reboot/foundation/types.hpp"
#include "reboot/ports/process.hpp"
#include "reboot/process/child_exit_info.hpp"

namespace rb::gameserver {

using MatchState = contracts::game_server::MatchState;
using ListenStage = contracts::game_server::ListenStage;
using MatchEndReason = contracts::game_server::MatchEndReason;
using BoundSocket = contracts::game_server::BoundSocket;
using Placement = contracts::game_server::Placement;

// `account_id` is what the client claimed; the server has not verified it.
struct Player {
    u32 player_id = 0;
    std::string account_id;
    std::string display_name;
    std::string address;

    bool operator==(const Player&) const = default;
};

// Every requested port, in declaration order, with its role.
struct Listening {
    std::vector<BoundSocket> bound;
};

// `bound_instead` is set when the server reported a port other than the one requested, which the
// no-fallback rule makes a failure too. Either way the process is already being stopped.
struct ListenFailed {
    Port port;
    ListenStage stage{};
    std::optional<SystemError> os_error;
    std::optional<Port> bound_instead;
};

struct MatchStateChanged {
    MatchState state{};
};

struct PlayerJoined {
    Player player;
};

// The roster entry of the leaving player.
struct PlayerLeft {
    Player player;
};

struct PlayerCountChanged {
    u32 count = 0;
};

struct MatchEnded {
    MatchEndReason reason{};
    std::optional<std::string> winner;
    std::vector<Placement> placements;
};

struct ServerFatal {
    std::string code;
    std::string detail;
};

// Report-only liveness: the host policy decides whether to stop the server.
struct ServerUnresponsive {
    int missed = 0;
};

// Always the last event of a process. Requested covers stop() and a ListenFailed.
struct ServerExited {
    process::ChildExitCause cause{};
    ports::ChildExit status;
    std::optional<Diagnostic> error;
};

// The server's Log frames are not events: ChildSupervisor writes them to the session log.
using GameServerEvent = std::variant<Listening, ListenFailed, MatchStateChanged, PlayerJoined, PlayerLeft,
                                     PlayerCountChanged, MatchEnded, ServerFatal, ServerUnresponsive, ServerExited>;

}  // namespace rb::gameserver
