#pragma once

#include "reboot/contracts/game_client.hpp"
#include "reboot/foundation/version.hpp"
#include "reboot/injection/net_mode.hpp"

namespace reboot::injection {

// The ClientDllConfig wire struct, so game_channel sends the plan's value unchanged.
using ClientFeatures = contracts::game_client::DllFeatures;

// Capabilities: dll-injection.timing.
// auth_redirect: off in LegacyFixed, where the custom DLL redirects.
// console: on, since injection serves play sessions only and those are always clients.
// memory_fix: major below 10; GameVersion is strictly parsed, so a guessed version never fires it.
[[nodiscard]] constexpr ClientFeatures client_features(const GameVersion& version, NetMode mode) noexcept {
    return ClientFeatures{
        .auth_redirect = mode == NetMode::Isolated,
        .console = true,
        .memory_fix = version.major < 10,
        .exit_suppression = true,
    };
}

}  // namespace reboot::injection
