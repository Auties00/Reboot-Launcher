#pragma once

#include <chrono>
#include <cstddef>

#include "reboot/foundation/clock.hpp"
#include "reboot/foundation/events.hpp"
#include "reboot/foundation/executor.hpp"
#include "reboot/foundation/operation.hpp"
#include "reboot/foundation/types.hpp"
#include "reboot/foundation/user_request.hpp"

namespace rb::testing {

// Covers no capability ids (decisions testing-strategy, async-event-model).
// The engine's strand machinery on manual time. The ManualExecutor stands in for both the strand
// and the I/O thread, so nothing runs until the test pumps it and every interleaving is chosen.
class DeterministicRuntime {
public:
    explicit DeterministicRuntime(EngineEpoch epoch = EngineEpoch{1})
        : strand_(clock_), timers_(clock_, strand_), events_(epoch), ops_(clock_, timers_, events_), requests_(events_) {}
    DeterministicRuntime(const DeterministicRuntime&) = delete;
    DeterministicRuntime& operator=(const DeterministicRuntime&) = delete;

    [[nodiscard]] ManualClock& clock() noexcept { return clock_; }
    [[nodiscard]] ManualExecutor& strand() noexcept { return strand_; }
    [[nodiscard]] TimerService& timers() noexcept { return timers_; }
    [[nodiscard]] EventBus& events() noexcept { return events_; }
    [[nodiscard]] OpRegistry& ops() noexcept { return ops_; }
    [[nodiscard]] UserRequestRegistry& requests() noexcept { return requests_; }

    // Runs ready tasks, including the ones they post, until none is left; returns how many ran.
    std::size_t run_until_idle() { return strand_.run_all(); }

    // Moves time in steps of at most `step`, running due work after each, so a timer armed by
    // another timer still fires inside the window.
    std::size_t advance(std::chrono::steady_clock::duration by,
                        std::chrono::steady_clock::duration step = std::chrono::milliseconds{100});

private:
    ManualClock clock_;
    ManualExecutor strand_;
    TimerService timers_;
    EventBus events_;
    OpRegistry ops_;
    UserRequestRegistry requests_;
};

}  // namespace rb::testing
