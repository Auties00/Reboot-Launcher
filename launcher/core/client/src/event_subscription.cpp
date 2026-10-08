#include "event_subscription.hpp"

#include <utility>

#include "api_event_kind.hpp"

namespace reboot::client {

namespace {

[[nodiscard]] bool is_op_completed(const contracts::ipc::WireEvent& event) noexcept {
    return event.kind == static_cast<u32>(ApiEventKind::OpCompleted) && event.op.has_value();
}

}  // namespace

EventSubscription::EventSubscription(u64 sub_id, std::vector<u8> filter, ApiEventFilter decoded, u64 link)
    : sub_id_(sub_id), filter_(std::move(filter)), decoded_(std::move(decoded)), link_(link) {}

void EventSubscription::follow(u64 op_id) {
    if (!admits_op_completed(decoded_, op_id)) return;
    std::lock_guard lock(mutex_);
    following_.insert(op_id);
}

void EventSubscription::unfollow(u64 op_id) {
    std::lock_guard lock(mutex_);
    following_.erase(op_id);
}

void EventSubscription::forget_delivered() {
    std::lock_guard lock(mutex_);
    delivered_.clear();
}

void EventSubscription::set_link(u64 link) {
    std::lock_guard lock(mutex_);
    link_ = link;
}

PushEffects EventSubscription::push_from_engine(std::vector<contracts::ipc::WireEvent> events, u64 link) {
    std::lock_guard lock(mutex_);
    PushEffects effects;
    if (closed_) return effects;
    for (auto& event : events) {
        if (is_op_completed(event)) {
            // A followed op's OpCompleted comes from its Outcome instead.
            if (following_.contains(*event.op) || delivered_.erase(*event.op) != 0) {
                ++effects.credit;
                continue;
            }
        }
        queue_locked(Queued{std::move(event), link});
    }
    effects.wake = arm_wake_locked();
    return effects;
}

PushEffects EventSubscription::push_local(contracts::ipc::WireEvent event) {
    std::lock_guard lock(mutex_);
    if (closed_) return {};
    queue_locked(Queued{std::move(event), 0});
    return PushEffects{arm_wake_locked(), 0};
}

PushEffects EventSubscription::push_outcome(u64 op_id, const contracts::ipc::WireEvent& op_completed) {
    std::lock_guard lock(mutex_);
    if (closed_ || following_.erase(op_id) == 0) return {};
    delivered_.insert(op_id);
    queue_locked(Queued{op_completed, 0});
    return PushEffects{arm_wake_locked(), 0};
}

TakenEvent EventSubscription::take() {
    std::lock_guard lock(mutex_);
    return take_locked();
}

u64 EventSubscription::open_wait() {
    std::lock_guard lock(mutex_);
    const u64 ticket = next_ticket_++;
    open_waits_.insert(ticket);
    return ticket;
}

TakenEvent EventSubscription::take_waiting(u64 ticket) {
    std::unique_lock lock(mutex_);
    changed_.wait(lock, [&] { return !events_.empty() || closed_ || expired_waits_.contains(ticket); });
    open_waits_.erase(ticket);
    expired_waits_.erase(ticket);
    return take_locked();
}

void EventSubscription::expire_wait(u64 ticket) {
    std::lock_guard lock(mutex_);
    if (!open_waits_.contains(ticket)) return;
    expired_waits_.insert(ticket);
    changed_.notify_all();
}

void EventSubscription::set_wake(WakeCallback wake) {
    std::lock_guard lock(mutex_);
    wake_ = wake;
    wake_fired_ = false;
}

std::optional<WakeCallback> EventSubscription::arm_wake() {
    std::lock_guard lock(mutex_);
    return arm_wake_locked();
}

void EventSubscription::acquire() {
    std::lock_guard lock(mutex_);
    ++users_;
}

void EventSubscription::release() {
    std::lock_guard lock(mutex_);
    --users_;
    changed_.notify_all();
}

void EventSubscription::close() {
    std::lock_guard lock(mutex_);
    closed_ = true;
    following_.clear();
    changed_.notify_all();
}

void EventSubscription::wait_unused() {
    std::unique_lock lock(mutex_);
    changed_.wait(lock, [&] { return users_ == 0; });
}

void EventSubscription::queue_locked(Queued queued) {
    events_.push_back(std::move(queued));
    changed_.notify_all();
}

std::optional<WakeCallback> EventSubscription::arm_wake_locked() {
    if (wake_.fn == nullptr || wake_fired_ || events_.empty()) return std::nullopt;
    wake_fired_ = true;
    return wake_;
}

TakenEvent EventSubscription::take_locked() {
    TakenEvent taken;
    if (events_.empty()) {
        taken.closed = closed_;
        return taken;
    }
    Queued front = std::move(events_.front());
    events_.pop_front();
    if (front.link != 0 && front.link == link_) taken.credit = 1;
    taken.event = std::move(front.event);
    if (events_.empty()) wake_fired_ = false;
    return taken;
}

}  // namespace reboot::client
