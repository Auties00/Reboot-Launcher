#pragma once

#include <optional>
#include <string>

#include "reboot/backend/backend_target.hpp"
#include "reboot/foundation/types.hpp"
#include "reboot/play/display_context.hpp"
#include "reboot/play/play_target.hpp"
#include "reboot/sessions/lease.hpp"

namespace reboot::play {

// Covers game-launch.orchestration.
// What Play.plan and Play.start receive.
struct PlayRequest {
    // Absent: the library's client selection.
    std::optional<BuildId> build;
    // Absent: GameServerTarget::current(); with neither, no match target is published and
    // matchmaking falls back to a game server on this machine.
    std::optional<PlayTarget> target;
    // Absent: the configured backend target.
    std::optional<backend::BackendTarget> backend;
    // Parsed after the settings' play.custom_args, so its keys win.
    std::string custom_args;
    // Never sent as such: ApiRouter fills it from the call's environment or the connection's Hello.
    DisplayContext display;
    sessions::Lease lease = sessions::Lease::engine();
};

}  // namespace reboot::play
