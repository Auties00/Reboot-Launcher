#include "reboot/sessions/shutdown_coordinator.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <optional>
#include <utility>
#include <vector>

#include "reboot/foundation/clock.hpp"
#include "reboot/foundation/executor.hpp"
#include "reboot/sessions/shutdown_budget.hpp"

namespace rb::sessions {

namespace {

[[nodiscard]] std::chrono::milliseconds since(const IClock& clock, SteadyTime start) {
    return std::chrono::duration_cast<std::chrono::milliseconds>(clock.steady_now() - start);
}

}  // namespace

struct ShutdownCoordinator::State {
    IClock& clock;
    TimerService& timers;
    Executor& strand;
    std::array<ShutdownAction, kShutdownStepCount> actions;
    std::optional<ShutdownCause> cause;
    std::vector<UniqueFunction<void(const ShutdownReport&)>> waiters;
    std::optional<ShutdownReport> finished;
    ShutdownReport report;
    SteadyTime started_at;
    std::size_t step = 0;
    SteadyTime step_started_at;
    // Bumped per step, so a `done` or timeout of an earlier step is ignored.
    u64 attempt = 0;
    CancelSource expiry;
    TimerHandle step_timer;
    // Cancelled by the destructor, so a posted step or a late `done` finds no state to touch.
    CancelSource alive;

    void post(UniqueFunction<void()> task) {
        strand.post([alive = alive.token(), task = std::move(task)]() mutable {
            if (!alive.cancelled()) task();
        });
    }

    void run_step() {
        if (step == kShutdownStepCount) return finish();
        const auto current = static_cast<ShutdownStep>(step);
        auto budget = step_budget(current, *cause);
        if (!always_runs(current)) budget = std::min(budget, total_shutdown_budget(*cause) - since(clock, started_at));
        ShutdownAction& action = actions[step];
        if (!action || budget <= std::chrono::milliseconds::zero())
            return record(StepOutcome::Skipped, std::nullopt, std::chrono::milliseconds::zero());

        const u64 this_attempt = ++attempt;
        step_started_at = clock.steady_now();
        expiry = CancelSource();
        step_timer = timers.after(budget, [this, this_attempt] {
            if (this_attempt != attempt) return;
            // Recorded first, so a `done` the action calls from its expiry callback is ignored.
            record(StepOutcome::TimedOut, std::nullopt, since(clock, step_started_at));
            expiry.cancel(CancelReason::Deadline);
        });
        action(ShutdownStepContext{.cause = *cause, .budget = budget, .expired = expiry.token()},
               [this, alive = alive.token(), this_attempt](Result<void> result) {
                   if (alive.cancelled() || this_attempt != attempt) return;
                   if (result) record(StepOutcome::Completed, std::nullopt, since(clock, step_started_at));
                   else record(StepOutcome::Failed, std::move(result.error()), since(clock, step_started_at));
               });
    }

    // Moves to the next step through the strand, so an action completing inside its call never recurses.
    void record(StepOutcome outcome, std::optional<Diagnostic> error, std::chrono::milliseconds elapsed) {
        ++attempt;
        step_timer.cancel();
        report.steps.push_back(ShutdownStepReport{.step = static_cast<ShutdownStep>(step),
                                                  .outcome = outcome,
                                                  .error = std::move(error),
                                                  .elapsed = elapsed});
        ++step;
        post([this] { run_step(); });
    }

    void finish() {
        report.elapsed = since(clock, started_at);
        finished = report;
        for (auto& waiter : std::exchange(waiters, {})) post_done(std::move(waiter));
    }

    void post_done(UniqueFunction<void(const ShutdownReport&)> done) {
        strand.post([done = std::move(done), report = *finished]() mutable { done(report); });
    }
};

ShutdownCoordinator::ShutdownCoordinator(IClock& clock, TimerService& timers, Executor& strand)
    : state_(std::make_unique<State>(State{.clock = clock,
                                           .timers = timers,
                                           .strand = strand,
                                           .actions = {},
                                           .cause = {},
                                           .waiters = {},
                                           .finished = {},
                                           .report = {},
                                           .started_at = {},
                                           .step = 0,
                                           .step_started_at = {},
                                           .attempt = 0,
                                           .expiry = {},
                                           .step_timer = {},
                                           .alive = {}})) {}

ShutdownCoordinator::~ShutdownCoordinator() { state_->alive.cancel(CancelReason::Shutdown); }

void ShutdownCoordinator::set_action(ShutdownStep step, ShutdownAction action) {
    if (state_->cause) return;
    state_->actions[static_cast<std::size_t>(step)] = std::move(action);
}

void ShutdownCoordinator::run(ShutdownCause cause, UniqueFunction<void(const ShutdownReport&)> done) {
    if (state_->finished) return state_->post_done(std::move(done));
    state_->waiters.push_back(std::move(done));
    if (state_->cause) return;
    state_->cause = cause;
    state_->report.cause = cause;
    state_->started_at = state_->clock.steady_now();
    state_->post([state = state_.get()] { state->run_step(); });
}

bool ShutdownCoordinator::started() const noexcept { return state_->cause.has_value(); }

}  // namespace rb::sessions
