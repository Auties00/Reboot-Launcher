#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <cstddef>
#include <optional>
#include <utility>
#include <vector>

#include "reboot/sessions/shutdown_budget.hpp"
#include "reboot/sessions/shutdown_coordinator.hpp"
#include "reboot/testing/deterministic_runtime.hpp"

using namespace reboot;
using namespace reboot::sessions;
using namespace std::chrono_literals;

namespace {

constexpr std::chrono::milliseconds budget_sum(ShutdownCause cause) {
    std::chrono::milliseconds sum{0};
    for (std::size_t i = 0; i < kShutdownStepCount; ++i) sum += step_budget(static_cast<ShutdownStep>(i), cause);
    return sum;
}

constexpr bool budgets_fit_totals() {
    for (const auto cause : {ShutdownCause::Requested, ShutdownCause::Idle, ShutdownCause::OsSignal,
                             ShutdownCause::DrainUpdate, ShutdownCause::DrainUserStop, ShutdownCause::DrainReplace})
        if (budget_sum(cause) > total_shutdown_budget(cause)) return false;
    return true;
}

using StepDone = UniqueFunction<void(Result<void>)>;

struct Fixture {
    reboot::testing::DeterministicRuntime runtime;
    ShutdownCoordinator coordinator{runtime.clock(), runtime.timers(), runtime.strand()};
    std::vector<ShutdownStep> ran;
    std::vector<StepDone> held;
    std::optional<CancelToken> stop_play_expired;

    void completes(ShutdownStep step) {
        coordinator.set_action(step, [this, step](const ShutdownStepContext&, StepDone done) {
            ran.push_back(step);
            done(Result<void>{});
        });
    }

    void hangs(ShutdownStep step) {
        coordinator.set_action(step, [this, step](const ShutdownStepContext& context, StepDone done) {
            ran.push_back(step);
            if (step == ShutdownStep::StopPlay) stop_play_expired = context.expired;
            held.push_back(std::move(done));
        });
    }
};

}  // namespace

TEST_CASE("every cause's step budgets fit its total, and an OsSignal shutdown fits a closed console's 5 s") {
    STATIC_CHECK(budgets_fit_totals());
    STATIC_CHECK(total_shutdown_budget(ShutdownCause::OsSignal) < 5s);
    STATIC_CHECK(kShutdownStepCount == 9);
}

TEST_CASE("steps run in order and done is posted, never inside run") {
    Fixture f;
    for (std::size_t i = 0; i < kShutdownStepCount; ++i) f.completes(static_cast<ShutdownStep>(i));
    std::optional<ShutdownReport> report;
    f.coordinator.run(ShutdownCause::Requested, [&](const ShutdownReport& done) { report = done; });
    CHECK(f.coordinator.started());
    CHECK(f.ran.empty());
    CHECK_FALSE(report);

    f.runtime.run_until_idle();
    REQUIRE(report);
    REQUIRE(f.ran.size() == kShutdownStepCount);
    REQUIRE(report->steps.size() == kShutdownStepCount);
    for (std::size_t i = 0; i < kShutdownStepCount; ++i) {
        CHECK(f.ran[i] == static_cast<ShutdownStep>(i));
        CHECK(report->steps[i].step == static_cast<ShutdownStep>(i));
        CHECK(report->steps[i].outcome == StepOutcome::Completed);
    }
}

TEST_CASE("a step without an action is skipped") {
    Fixture f;
    f.completes(ShutdownStep::FlushLogs);
    std::optional<ShutdownReport> report;
    f.coordinator.run(ShutdownCause::Idle, [&](const ShutdownReport& done) { report = done; });
    f.runtime.run_until_idle();
    REQUIRE(report);
    CHECK(report->steps.front().outcome == StepOutcome::Skipped);
    CHECK(report->steps.back().outcome == StepOutcome::Completed);
}

TEST_CASE("a late step times out without blocking the next, and its late done is ignored") {
    Fixture f;
    f.hangs(ShutdownStep::StopPlay);
    f.completes(ShutdownStep::FlushStores);
    std::optional<ShutdownReport> report;
    f.coordinator.run(ShutdownCause::Requested, [&](const ShutdownReport& done) { report = done; });
    f.runtime.run_until_idle();
    CHECK_FALSE(report);
    REQUIRE(f.stop_play_expired);
    CHECK_FALSE(f.stop_play_expired->cancelled());

    f.runtime.advance(step_budget(ShutdownStep::StopPlay, ShutdownCause::Requested));
    REQUIRE(report);
    CHECK(f.stop_play_expired->reason() == CancelReason::Deadline);
    CHECK(report->steps[static_cast<std::size_t>(ShutdownStep::StopPlay)].outcome == StepOutcome::TimedOut);
    CHECK(report->steps[static_cast<std::size_t>(ShutdownStep::FlushStores)].outcome == StepOutcome::Completed);

    f.held.front()(Result<void>{});
    f.runtime.run_until_idle();
    CHECK(report->steps[static_cast<std::size_t>(ShutdownStep::StopPlay)].outcome == StepOutcome::TimedOut);
}

TEST_CASE("an OsSignal shutdown with every step hanging still reaches the flush steps within its total") {
    Fixture f;
    for (std::size_t i = 0; i < kShutdownStepCount; ++i) f.hangs(static_cast<ShutdownStep>(i));
    std::optional<ShutdownReport> report;
    f.coordinator.run(ShutdownCause::OsSignal, [&](const ShutdownReport& done) { report = done; });
    f.runtime.advance(total_shutdown_budget(ShutdownCause::OsSignal), 10ms);
    REQUIRE(report);
    CHECK(f.ran.size() == kShutdownStepCount);
    CHECK(report->elapsed <= total_shutdown_budget(ShutdownCause::OsSignal));
    for (const ShutdownStepReport& step : report->steps) CHECK(step.outcome == StepOutcome::TimedOut);
}

TEST_CASE("run is idempotent and the first cause wins") {
    Fixture f;
    f.completes(ShutdownStep::RefuseNew);
    std::vector<ShutdownCause> causes;
    f.coordinator.run(ShutdownCause::Requested, [&](const ShutdownReport& done) { causes.push_back(done.cause); });
    f.coordinator.run(ShutdownCause::OsSignal, [&](const ShutdownReport& done) { causes.push_back(done.cause); });
    f.completes(ShutdownStep::FlushLogs);
    f.runtime.run_until_idle();
    CHECK(causes == std::vector{ShutdownCause::Requested, ShutdownCause::Requested});
    CHECK(f.ran == std::vector{ShutdownStep::RefuseNew});

    f.coordinator.run(ShutdownCause::Idle, [&](const ShutdownReport& done) { causes.push_back(done.cause); });
    CHECK(causes.size() == 2);
    f.runtime.run_until_idle();
    CHECK(causes.size() == 3);
}
