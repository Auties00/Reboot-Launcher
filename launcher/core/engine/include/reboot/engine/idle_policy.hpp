#pragma once

#include <chrono>
#include <cstddef>

#include "reboot/engine/engine_origin.hpp"
#include "reboot/foundation/executor.hpp"
#include "reboot/foundation/function.hpp"

namespace reboot::engine {

// Capabilities: none. Strand-only; decides when the engine may end on its own.
class IdlePolicy {
public:
    static constexpr std::chrono::minutes kIdleExitDelay{5};

    IdlePolicy(TimerService& timers, EngineOrigin origin, UniqueFunction<void()> on_idle_exit,
               std::chrono::steady_clock::duration delay = kIdleExitDelay);
    IdlePolicy(const IdlePolicy&) = delete;
    IdlePolicy& operator=(const IdlePolicy&) = delete;
    ~IdlePolicy();

    // Called on every change; going busy again cancels a pending exit.
    void update(std::size_t connections, bool work_live);

    // Connected clients do not hold it: they get Goodbye and reconnect. One waiter at a time,
    // which EngineLifecycle guarantees.
    void when_work_idle(UniqueFunction<void()> on_idle);

    // For an engine that is already shutting down: nothing fires any more.
    void disable() noexcept;

    [[nodiscard]] bool exit_pending() const noexcept;
    [[nodiscard]] bool work_live() const noexcept { return work_live_; }

private:
    void evaluate();

    TimerService& timers_;
    bool exits_when_idle_;
    std::chrono::steady_clock::duration delay_;
    UniqueFunction<void()> on_idle_exit_;
    UniqueFunction<void()> work_idle_waiter_;
    TimerHandle exit_timer_;
    std::size_t connections_ = 0;
    bool work_live_ = false;
    bool disabled_ = false;
};

}  // namespace reboot::engine
