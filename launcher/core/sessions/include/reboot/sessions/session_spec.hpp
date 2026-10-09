#pragma once

#include <optional>
#include <string>

#include "reboot/foundation/types.hpp"
#include "reboot/foundation/version.hpp"
#include "reboot/ports/runner.hpp"
#include "reboot/sessions/lease.hpp"
#include "reboot/sessions/pinned_inputs.hpp"
#include "reboot/sessions/session_kind.hpp"

namespace rb::sessions {

struct SessionSpec {
    SessionKind kind = SessionKind::Play;
    Lease lease;
    PinnedInputs pinned;
    // The play session a linked auto-server belongs to; it must be live.
    std::optional<SessionId> parent;
    // The build or host profile name.
    std::string label;
    GameVersion version;
    // Native for every host.
    ports::RunnerKind runner = ports::RunnerKind::Native;
    std::optional<HostProfileId> profile;
};

}  // namespace rb::sessions
