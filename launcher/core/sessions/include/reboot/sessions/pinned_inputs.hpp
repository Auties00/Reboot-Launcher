#pragma once

#include <array>
#include <optional>
#include <string>

#include "reboot/base/types.hpp"
#include "reboot/base/version.hpp"
#include "reboot/storage/settings.hpp"

namespace rb::sessions {

// Fixed at open: a later settings edit, payload or runtime update never reaches a running session.
struct PinnedInputs {
    storage::SettingsSnapshot settings;
    // Absent for a host whose game server needs only the game version.
    std::optional<BuildId> build;
    // Play only: the client DLL payload, pinned by SessionRegistry::pin_payload during preflight.
    std::optional<SemVer> payload_version;
    // Play under Wine only: the runtime component id.
    std::optional<std::string> runtime_id;
    // Host only: the reboot-game-server binary the description cache is keyed by.
    std::optional<std::array<u8, 32>> game_server_sha256;
};

}  // namespace rb::sessions
