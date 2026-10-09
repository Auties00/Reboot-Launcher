#pragma once

#include "reboot/foundation/types.hpp"

namespace rb::sessions {

enum class ProcessRole : u8 { Game, Companion, GameServer, Winhost };

// The game for play, the game server for host.
[[nodiscard]] constexpr bool is_primary(ProcessRole role) noexcept {
    return role == ProcessRole::Game || role == ProcessRole::GameServer;
}

// Under Wine, processes started by winhost carry guest Windows pids; winhost itself has a host pid.
enum class PidSpace : u8 { Host, GuestWindows };

struct SpawnedProcess {
    ProcessRole role{};
    PidSpace space = PidSpace::Host;
    u32 pid = 0;

    bool operator==(const SpawnedProcess&) const = default;
};

// Counts primary spawns in one session, so an earlier primary's late exit is never the current one's.
struct Incarnation {
    u32 value = 0;

    constexpr auto operator<=>(const Incarnation&) const = default;
};

}  // namespace rb::sessions
