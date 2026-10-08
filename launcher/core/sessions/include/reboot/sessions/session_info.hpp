#pragma once

#include <chrono>
#include <optional>
#include <string>
#include <vector>

#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/types.hpp"
#include "reboot/foundation/version.hpp"
#include "reboot/ports/runner.hpp"
#include "reboot/sessions/lease.hpp"
#include "reboot/sessions/pinned_inputs.hpp"
#include "reboot/sessions/session_kind.hpp"
#include "reboot/sessions/session_phase.hpp"
#include "reboot/sessions/spawned_process.hpp"
#include "reboot/sessions/stop_reason.hpp"

namespace reboot::sessions {

struct SessionInfo {
    SessionId id;
    SessionKind kind = SessionKind::Play;
    SessionPhase phase = SessionPhase::Preparing;
    Lease lease;
    PinnedInputs pinned;
    std::optional<SessionId> parent;
    std::vector<SessionId> children;
    std::string label;
    GameVersion version;
    ports::RunnerKind runner = ports::RunnerKind::Native;
    std::optional<HostProfileId> profile;
    std::chrono::system_clock::time_point started_at;
    // Live processes only.
    std::vector<SpawnedProcess> processes;
    // Problems that did not end the session, at most one per message id.
    std::vector<Diagnostic> degraded;
    // Set once the session is Stopping.
    std::optional<StopReason> stop_reason;
};

}  // namespace reboot::sessions
