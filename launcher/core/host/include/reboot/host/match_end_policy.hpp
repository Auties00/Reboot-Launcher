#pragma once

#include <chrono>

#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/types.hpp"
#include "reboot/gameserver/game_server_description.hpp"

namespace reboot::host {

enum class MatchEndAction : u8 { Restart, Shutdown, None };

// 10.x waited a fixed 10 s; the delay is the cancellable Restarting window.
inline constexpr std::chrono::seconds kDefaultMatchEndDelay{10};
inline constexpr std::chrono::seconds kMaxMatchEndDelay{600};

struct MatchEndPolicy {
    MatchEndAction action = MatchEndAction::Restart;
    std::chrono::seconds delay = kDefaultMatchEndDelay;

    bool operator==(const MatchEndPolicy&) const = default;
};

// host.invalid_match_end_delay for a negative delay or one above kMaxMatchEndDelay.
[[nodiscard]] Result<void> validate(const MatchEndPolicy& policy);

// Reset keeps the process and its port block; Respawn starts a new process on the same block.
enum class RestartMethod : u8 { InProcessReset, Respawn };

[[nodiscard]] constexpr RestartMethod restart_method(const gameserver::GameServerCapabilities& capabilities) noexcept {
    return capabilities.in_process_reset ? RestartMethod::InProcessReset : RestartMethod::Respawn;
}

}  // namespace reboot::host
