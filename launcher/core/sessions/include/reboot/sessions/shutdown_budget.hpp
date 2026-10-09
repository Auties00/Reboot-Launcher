#pragma once

#include <chrono>

#include "reboot/foundation/operation.hpp"
#include "reboot/sessions/shutdown_cause.hpp"
#include "reboot/sessions/shutdown_step.hpp"

namespace rb::sessions {

// Windows ends a closed console's process 5 s after the signal, so an OsSignal shutdown must fit 4 s.
inline constexpr std::chrono::milliseconds kOsSignalShutdownBudget{4000};

[[nodiscard]] constexpr std::chrono::milliseconds total_shutdown_budget(ShutdownCause cause) noexcept {
    return cause == ShutdownCause::OsSignal ? kOsSignalShutdownBudget : default_deadline(OpKind::ShutdownBudget);
}

// Each cause's step budgets sum to at most its total, so the flush steps always get their turn.
[[nodiscard]] constexpr std::chrono::milliseconds step_budget(ShutdownStep step, ShutdownCause cause) noexcept {
    using std::chrono::milliseconds;
    const bool os_signal = cause == ShutdownCause::OsSignal;
    // A stop step covers one session's grace plus kStopKillMargin and a little slack.
    const milliseconds stop = os_signal ? milliseconds{800} : milliseconds{6500};
    switch (step) {
        case ShutdownStep::RefuseNew: return milliseconds{os_signal ? 100 : 500};
        case ShutdownStep::Unpublish: return milliseconds{os_signal ? 300 : 1500};
        case ShutdownStep::UnmapPorts: return milliseconds{os_signal ? 300 : 2500};
        case ShutdownStep::DrainHosts: return stop;
        case ShutdownStep::StopPlay: return stop;
        case ShutdownStep::StopBackend: return os_signal ? milliseconds{400} : stop;
        case ShutdownStep::CloseListeners: return milliseconds{os_signal ? 100 : 500};
        case ShutdownStep::FlushStores: return milliseconds{os_signal ? 800 : 3000};
        case ShutdownStep::FlushLogs: return milliseconds{os_signal ? 300 : 2000};
    }
    return milliseconds{0};
}

}  // namespace rb::sessions
