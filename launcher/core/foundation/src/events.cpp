#include "reboot/foundation/events.hpp"

#include <algorithm>

namespace rb {

Subscription::Subscription(EventFilter filter, std::size_t byte_budget)
    : filter_(std::move(filter)), byte_budget_(byte_budget) {}

std::size_t Subscription::drain(std::vector<EventEnvelope>& out, std::size_t max) {
    std::size_t taken = 0;
    while (taken < max && !queue_.empty()) {
        used_bytes_ -= queue_.front().approx_bytes;
        out.push_back(std::move(queue_.front()));
        queue_.pop_front();
        ++taken;
    }
    return taken;
}

bool Subscription::matches(const EventEnvelope& event) const {
    if (!filter_.kinds.empty() && std::ranges::find(filter_.kinds, event.kind) == filter_.kinds.end()) return false;
    if (filter_.session && event.session != filter_.session) return false;
    if (filter_.op && event.op != filter_.op) return false;
    return true;
}

void Subscription::offer(const EventEnvelope& event) {
    // Taken before coalescing, so replacing a queued event does not notify again.
    const bool was_empty = queue_.empty();
    const DeliveryClass delivery = delivery_class(event.kind);
    if (delivery == DeliveryClass::CoalesceLatest) {
        const auto older = std::ranges::find_if(queue_, [&](const EventEnvelope& queued) {
            return queued.kind == event.kind && queued.coalesce_key == event.coalesce_key;
        });
        // The newer copy goes to the back so seq stays increasing.
        if (older != queue_.end()) {
            used_bytes_ -= older->approx_bytes;
            queue_.erase(older);
        }
    }

    if (used_bytes_ + event.approx_bytes > byte_budget_) {
        if (delivery == DeliveryClass::DropWithCounter) {
            ++dropped_;
            return;
        }
        // Lossy events make room first; past that the reader must re-fetch state.
        const auto lossy = [](const EventEnvelope& queued) {
            return delivery_class(queued.kind) == DeliveryClass::DropWithCounter;
        };
        for (auto it = queue_.begin(); it != queue_.end() && used_bytes_ + event.approx_bytes > byte_budget_;) {
            if (!lossy(*it)) {
                ++it;
                continue;
            }
            used_bytes_ -= it->approx_bytes;
            it = queue_.erase(it);
            ++dropped_;
        }
        if (used_bytes_ + event.approx_bytes > byte_budget_) {
            queue_.clear();
            used_bytes_ = 0;
            const bool newly_pending = !resync_;
            resync_ = true;
            if (newly_pending && notify_) notify_();
            return;
        }
    }

    queue_.push_back(event);
    used_bytes_ += event.approx_bytes;
    if (was_empty && notify_) notify_();
}

std::shared_ptr<Subscription> EventBus::subscribe(EventFilter filter, std::size_t byte_budget) {
    auto subscription = std::make_shared<Subscription>(std::move(filter), byte_budget);
    subscriptions_.push_back(subscription);
    return subscription;
}

void EventBus::dispatch(EventEnvelope event) {
    event.epoch = epoch_;
    event.seq = next_seq_++;
    if (dispatching_) {
        deferred_.push_back(std::move(event));
        return;
    }
    dispatching_ = true;
    deliver(event);
    while (!deferred_.empty()) {
        const EventEnvelope next = std::move(deferred_.front());
        deferred_.pop_front();
        deliver(next);
    }
    dispatching_ = false;
}

void EventBus::deliver(const EventEnvelope& event) {
    std::erase_if(subscriptions_, [](const std::weak_ptr<Subscription>& weak) { return weak.expired(); });
    // A notify callback may subscribe, so iterate over a snapshot.
    const std::vector<std::weak_ptr<Subscription>> targets = subscriptions_;
    for (const std::weak_ptr<Subscription>& weak : targets)
        if (const std::shared_ptr<Subscription> subscription = weak.lock(); subscription && subscription->matches(event))
            subscription->offer(event);
}

}  // namespace rb
