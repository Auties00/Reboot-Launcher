#pragma once

#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <mutex>
#include <optional>
#include <unordered_set>
#include <vector>

#include "api_event_filter.hpp"
#include "reboot/contracts/ipc.hpp"
#include "reboot/foundation/types.hpp"

namespace rb::client {

struct WakeCallback {
    void (*fn)(std::uintptr_t) = nullptr;
    std::uintptr_t user = 0;
};

// What a push leaves for its caller to do once no lock is held.
struct PushEffects {
    std::optional<WakeCallback> wake;
    // Engine events dropped as duplicates; returned to the engine as Credit at once.
    u32 credit = 0;
};

struct TakenEvent {
    std::optional<contracts::ipc::WireEvent> event;
    // 1 for an engine event of the current link, which goes back to the engine as Credit.
    u32 credit = 0;
    // Closed and drained.
    bool closed = false;
};

// Covers no capability ids. A thread-safe queue; a followed op's OpCompleted comes from its Outcome.
class EventSubscription {
public:
    EventSubscription(u64 sub_id, std::vector<u8> filter, ApiEventFilter decoded, u64 link);

    [[nodiscard]] u64 id() const noexcept { return sub_id_; }
    // Sent again in Subscribe after a reconnect.
    [[nodiscard]] const std::vector<u8>& filter() const noexcept { return filter_; }

    // An attached op not yet terminal; ignored unless the filter admits its OpCompleted.
    void follow(u64 op_id);
    // rb_op_release: the op's Outcome no longer reaches this context.
    void unfollow(u64 op_id);
    // A new epoch: the old engine sends nothing more for the ops already delivered.
    void forget_delivered();
    // A new link: events queued from older links no longer earn credit.
    void set_link(u64 link);

    [[nodiscard]] PushEffects push_from_engine(std::vector<contracts::ipc::WireEvent> events, u64 link);
    // Resync, ConnectionLost or Reconnected.
    [[nodiscard]] PushEffects push_local(contracts::ipc::WireEvent event);
    // Queues `op_completed` if this subscription follows the op.
    [[nodiscard]] PushEffects push_outcome(u64 op_id, const contracts::ipc::WireEvent& op_completed);

    [[nodiscard]] TakenEvent take();
    [[nodiscard]] u64 open_wait();
    // Blocks until an event is queued, the subscription closes or expire_wait(ticket) runs.
    [[nodiscard]] TakenEvent take_waiting(u64 ticket);
    void expire_wait(u64 ticket);

    // A null `fn` removes the wake. The new wake may run once for events already queued.
    void set_wake(WakeCallback wake);
    // The wake, when one is set, events are queued and it has not run since the queue last emptied.
    [[nodiscard]] std::optional<WakeCallback> arm_wake();

    // Held by a caller or a push while it uses the subscription; unsubscribe waits for none.
    void acquire();
    void release();
    // Wakes blocked takers; takes drain what is queued, then report closed.
    void close();
    void wait_unused();

private:
    struct Queued {
        contracts::ipc::WireEvent event;
        // 0 for a library event.
        u64 link = 0;
    };

    void queue_locked(Queued queued);
    [[nodiscard]] std::optional<WakeCallback> arm_wake_locked();
    [[nodiscard]] TakenEvent take_locked();

    const u64 sub_id_;
    const std::vector<u8> filter_;
    const ApiEventFilter decoded_;

    std::mutex mutex_;
    std::condition_variable changed_;
    std::deque<Queued> events_;
    u64 link_ = 0;
    // Followed ops whose Outcome has not arrived.
    std::unordered_set<u64> following_;
    // Ops whose OpCompleted was queued; the engine's own copy is dropped once, then forgotten.
    std::unordered_set<u64> delivered_;
    std::unordered_set<u64> open_waits_;
    std::unordered_set<u64> expired_waits_;
    u64 next_ticket_ = 1;
    WakeCallback wake_;
    bool wake_fired_ = false;
    bool closed_ = false;
    std::size_t users_ = 0;
};

}  // namespace rb::client
