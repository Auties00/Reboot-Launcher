#pragma once

#include <chrono>
#include <optional>
#include <vector>

#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/types.hpp"
#include "reboot/sessions/shutdown_cause.hpp"
#include "reboot/sessions/shutdown_step.hpp"

namespace reboot::sessions {

// Skipped: no action registered, or the total budget ran out before the step began.
enum class StepOutcome : u8 { Completed, Failed, TimedOut, Skipped };

struct ShutdownStepReport {
    ShutdownStep step = ShutdownStep::RefuseNew;
    StepOutcome outcome = StepOutcome::Completed;
    std::optional<Diagnostic> error;
    std::chrono::milliseconds elapsed{0};
};

// One entry per step, in run order.
struct ShutdownReport {
    ShutdownCause cause = ShutdownCause::Requested;
    std::vector<ShutdownStepReport> steps;
    std::chrono::milliseconds elapsed{0};
};

}  // namespace reboot::sessions
