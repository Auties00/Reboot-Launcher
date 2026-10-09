#include "reboot/engine/idle_policy.hpp"

#include <utility>

namespace rb::engine {

IdlePolicy::IdlePolicy(TimerService& timers, EngineOrigin origin, UniqueFunction<void()> on_idle_exit,
                       std::chrono::steady_clock::duration delay)
    : timers_(timers),
      exits_when_idle_(exits_when_idle(origin)),
      delay_(delay),
      on_idle_exit_(std::move(on_idle_exit)) {}

IdlePolicy::~IdlePolicy() = default;

void IdlePolicy::update(std::size_t connections, bool work_live) {
    connections_ = connections;
    work_live_ = work_live;
    evaluate();
}

void IdlePolicy::when_work_idle(UniqueFunction<void()> on_idle) {
    if (disabled_) return;
    work_idle_waiter_ = std::move(on_idle);
    evaluate();
}

void IdlePolicy::disable() noexcept {
    disabled_ = true;
    exit_timer_.cancel();
    work_idle_waiter_ = nullptr;
}

bool IdlePolicy::exit_pending() const noexcept { return exit_timer_.active(); }

void IdlePolicy::evaluate() {
    if (disabled_) return;
    if (!work_live_ && work_idle_waiter_) {
        UniqueFunction<void()> waiter = std::move(work_idle_waiter_);
        work_idle_waiter_ = nullptr;
        waiter();
        if (disabled_) return;
    }
    const bool idle = exits_when_idle_ && connections_ == 0 && !work_live_;
    if (!idle) {
        exit_timer_.cancel();
        return;
    }
    if (exit_timer_.active()) return;
    exit_timer_ = timers_.after(delay_, [this] {
        if (disabled_ || connections_ != 0 || work_live_) return;
        if (on_idle_exit_) on_idle_exit_();
    });
}

}  // namespace rb::engine
