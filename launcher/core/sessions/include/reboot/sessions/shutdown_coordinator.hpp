#pragma once

#include <chrono>
#include <memory>

#include "reboot/foundation/cancel.hpp"
#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/function.hpp"
#include "reboot/sessions/shutdown_cause.hpp"
#include "reboot/sessions/shutdown_report.hpp"
#include "reboot/sessions/shutdown_step.hpp"

namespace rb {
class IClock;
class Executor;
class TimerService;
}  // namespace rb

namespace rb::sessions {

struct ShutdownStepContext {
    ShutdownCause cause = ShutdownCause::Requested;
    // A stop step passes what is left after kStopKillMargin, if anything, as its grace.
    std::chrono::milliseconds budget{0};
    // Cancelled with Deadline when the budget runs out; the action should then give up.
    CancelToken expired;
};

// Calls `done` once on the strand; a call after the step timed out is ignored.
using ShutdownAction =
    UniqueFunction<void(const ShutdownStepContext& context, UniqueFunction<void(Result<void>)> done)>;

// Capabilities: game-launch.app-exit-cleanup, game-launch.+39.
// Strand-only. Runs the steps in order, each within its budget; a failed or late step never blocks the next.
class ShutdownCoordinator {
public:
    // `strand` runs every step and every `done`, never inside the caller's own call.
    ShutdownCoordinator(IClock& clock, TimerService& timers, Executor& strand);
    ~ShutdownCoordinator();
    ShutdownCoordinator(const ShutdownCoordinator&) = delete;
    ShutdownCoordinator& operator=(const ShutdownCoordinator&) = delete;

    // Replaces the step's earlier action; ignored once run() was called.
    void set_action(ShutdownStep step, ShutdownAction action);

    // Idempotent: the first cause wins, and each `done` is posted with the report after the last step.
    void run(ShutdownCause cause, UniqueFunction<void(const ShutdownReport&)> done);

    [[nodiscard]] bool started() const noexcept;

private:
    struct State;
    std::unique_ptr<State> state_;
};

}  // namespace rb::sessions
