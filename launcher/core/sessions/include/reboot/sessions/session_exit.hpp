#pragma once

#include <optional>

#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/types.hpp"
#include "reboot/sessions/stop_reason.hpp"

namespace reboot::sessions {

// Why a primary ended without being asked to.
enum class ExitReason : u8 { Exited, Crashed, Unresponsive, LaunchFailed, Fatal };

[[nodiscard]] constexpr StopReason stop_reason_for(ExitReason reason) noexcept {
    switch (reason) {
        case ExitReason::Exited: return StopReason::Exited;
        case ExitReason::Crashed: return StopReason::Crashed;
        case ExitReason::Unresponsive: return StopReason::Unresponsive;
        case ExitReason::LaunchFailed: return StopReason::LaunchFailed;
        case ExitReason::Fatal: return StopReason::Fatal;
    }
    return StopReason::Exited;
}

struct SessionExit {
    ExitReason reason = ExitReason::Exited;
    std::optional<i32> exit_code;
    std::optional<Diagnostic> error;
};

}  // namespace reboot::sessions
