#pragma once

#include <chrono>
#include <optional>

#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/operation.hpp"
#include "reboot/sessions/stop_reason.hpp"

namespace reboot::sessions {

// After the grace, the time a driver has to kill and clean up before the registry ends the session itself.
inline constexpr std::chrono::milliseconds kStopKillMargin{1000};

struct StopRequest {
    StopReason reason = StopReason::User;
    // Between the graceful request and the kill.
    std::chrono::milliseconds grace = default_deadline(OpKind::GracefulStop);
    // Reported in SessionEnded, e.g. why a launch failed.
    std::optional<Diagnostic> error;
};

}  // namespace reboot::sessions
