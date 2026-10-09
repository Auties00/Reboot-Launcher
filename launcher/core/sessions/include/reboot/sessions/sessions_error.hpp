#pragma once

#include <optional>

#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/types.hpp"
#include "reboot/sessions/session_phase.hpp"

namespace rb::sessions {

enum class SessionsErrorCode : u8 {
    NotFound,
    // The session ended and can no longer change.
    Ended,
    // The session is stopping and takes no respawn.
    Stopping,
    // Shutdown step RefuseNew ran.
    RefusingNew,
    ParentNotLive,
    // Stopping and Ended are entered only through the stop path.
    InvalidTransition,
    // The driver overran grace + kStopKillMargin; the registry ended the session itself.
    StopOverran,
};

struct SessionsError {
    SessionsErrorCode code = SessionsErrorCode::NotFound;
    std::optional<SessionId> session;
    std::optional<SessionPhase> from;
    std::optional<SessionPhase> to;
};

[[nodiscard]] Diagnostic to_diagnostic(const SessionsError& error);

}  // namespace rb::sessions
