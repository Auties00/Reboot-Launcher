#include "reboot/testing/event_recorder.hpp"

#include <algorithm>
#include <cstddef>
#include <limits>
#include <utility>
#include <vector>

namespace reboot::testing {

EventRecorder::EventRecorder(EventBus& bus, EventFilter filter, std::size_t byte_budget)
    : epoch_(bus.epoch()), subscription_(bus.subscribe(std::move(filter), byte_budget)) {}

std::size_t EventRecorder::pump() {
    if (subscription_->take_resync()) ++resyncs_;
    return subscription_->drain(events_, std::numeric_limits<std::size_t>::max());
}

std::size_t EventRecorder::count(EventKind kind) const {
    return static_cast<std::size_t>(std::ranges::count(events_, kind, &EventEnvelope::kind));
}

u64 EventRecorder::dropped() const { return subscription_->dropped(); }

bool EventRecorder::sequence_ok() const {
    u64 previous = 0;
    for (const EventEnvelope& event : events_) {
        if (event.epoch != epoch_ || event.seq <= previous) return false;
        previous = event.seq;
    }
    return true;
}

}  // namespace reboot::testing
