#pragma once

#include <any>
#include <cstddef>
#include <memory>
#include <vector>

#include "reboot/foundation/events.hpp"
#include "reboot/foundation/types.hpp"

namespace reboot::testing {

// Covers no capability ids (decisions testing-strategy, async-event-model).
// Subscribes to an EventBus the way one IPC connection does and keeps what it delivers, so tests
// can assert coalescing, the Resync path and that NeverDrop events arrive exactly once.
class EventRecorder {
public:
    // Large enough that only tests that shrink it see a Resync.
    static constexpr std::size_t kDefaultBudget = std::size_t{1} << 20;

    explicit EventRecorder(EventBus& bus, EventFilter filter = {}, std::size_t byte_budget = kDefaultBudget);

    // Drains the subscription and counts a pending Resync; returns how many events arrived.
    std::size_t pump();

    [[nodiscard]] const std::vector<EventEnvelope>& events() const noexcept { return events_; }
    [[nodiscard]] std::size_t count(EventKind kind) const;
    [[nodiscard]] std::size_t resyncs() const noexcept { return resyncs_; }
    [[nodiscard]] u64 dropped() const;
    // seq strictly increases and every event carries the bus epoch.
    [[nodiscard]] bool sequence_ok() const;

    template <class E>
    [[nodiscard]] std::vector<const E*> payloads(EventKind kind) const {
        std::vector<const E*> out;
        for (const EventEnvelope& event : events_)
            if (event.kind == kind)
                if (const E* payload = std::any_cast<E>(&event.payload)) out.push_back(payload);
        return out;
    }

    void clear() noexcept { events_.clear(); }

private:
    EngineEpoch epoch_;
    std::shared_ptr<Subscription> subscription_;
    std::vector<EventEnvelope> events_;
    std::size_t resyncs_ = 0;
};

}  // namespace reboot::testing
