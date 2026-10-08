#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <optional>
#include <vector>

#include "api_event_filter.hpp"
#include "api_event_kind.hpp"
#include "api_event_payload.hpp"
#include "event_subscription.hpp"
#include "reboot/contracts/ipc.hpp"

using namespace reboot;
using namespace reboot::client;
using contracts::ipc::WireEvent;

namespace {

constexpr u32 kSessionEnded = 15;

[[nodiscard]] WireEvent engine_event(u64 seq, u32 kind = kSessionEnded) {
    WireEvent event;
    event.kind = kind;
    event.epoch = 1;
    event.seq = seq;
    return event;
}

[[nodiscard]] WireEvent engine_op_completed(u64 op_id) {
    WireEvent event = engine_event(50, static_cast<u32>(ApiEventKind::OpCompleted));
    event.op = op_id;
    return event;
}

[[nodiscard]] EventSubscription subscription(ApiEventFilter filter = {}) { return {1, {}, std::move(filter), 1}; }

void wake(std::uintptr_t) {}

}  // namespace

TEST_CASE("only engine events of the current link earn credit", "[client][events]") {
    EventSubscription sub = subscription();
    CHECK(sub.push_from_engine({engine_event(1)}, 1).credit == 0);
    static_cast<void>(sub.push_local(library_event(ApiEventKind::Resync, 1)));
    CHECK(sub.push_from_engine({engine_event(2)}, 1).credit == 0);
    // A reconnect: the event queued from link 1 no longer counts against the new window.
    sub.set_link(2);
    static_cast<void>(sub.push_from_engine({engine_event(3)}, 2));

    CHECK(sub.take().credit == 0);
    const TakenEvent resync = sub.take();
    REQUIRE(resync.event);
    CHECK(resync.event->kind == static_cast<u32>(ApiEventKind::Resync));
    CHECK(resync.credit == 0);
    CHECK(sub.take().credit == 0);
    CHECK(sub.take().credit == 1);
    const TakenEvent empty = sub.take();
    CHECK_FALSE(empty.event);
    CHECK_FALSE(empty.closed);
}

TEST_CASE("the wake runs at most once until the queue has been taken empty", "[client][events]") {
    EventSubscription sub = subscription();
    sub.set_wake(WakeCallback{&wake, 7});

    const PushEffects first = sub.push_from_engine({engine_event(1)}, 1);
    REQUIRE(first.wake);
    CHECK(first.wake->user == 7);
    CHECK_FALSE(sub.push_from_engine({engine_event(2)}, 1).wake);

    REQUIRE(sub.take().event);
    CHECK_FALSE(sub.push_from_engine({engine_event(3)}, 1).wake);

    while (sub.take().event) {
    }
    CHECK(sub.push_from_engine({engine_event(4)}, 1).wake);
}

TEST_CASE("a wake set while events are queued is armed once; a null wake never runs", "[client][events]") {
    EventSubscription sub = subscription();
    static_cast<void>(sub.push_from_engine({engine_event(1)}, 1));
    CHECK_FALSE(sub.arm_wake());

    sub.set_wake(WakeCallback{&wake, 1});
    CHECK(sub.arm_wake());
    CHECK_FALSE(sub.arm_wake());

    sub.set_wake(WakeCallback{});
    CHECK_FALSE(sub.arm_wake());
}

TEST_CASE("a followed op's OpCompleted is delivered once from its Outcome", "[client][events]") {
    EventSubscription sub = subscription();
    sub.follow(9);
    const WireEvent outcome_event = op_completed_event(1, 9, std::vector<u8>{0x08, 0x09});

    // The engine's copy arrives first and is dropped, with its credit returned at once.
    CHECK(sub.push_from_engine({engine_op_completed(9)}, 1).credit == 1);
    CHECK_FALSE(sub.take().event);

    static_cast<void>(sub.push_outcome(9, outcome_event));
    // A replayed OpResult after a same-epoch reconnect.
    static_cast<void>(sub.push_outcome(9, outcome_event));

    const TakenEvent taken = sub.take();
    REQUIRE(taken.event);
    CHECK(taken.event->op == 9u);
    CHECK(taken.credit == 0);
    CHECK_FALSE(sub.take().event);
}

TEST_CASE("an OpCompleted the engine dropped before a Resync still arrives", "[client][events]") {
    EventSubscription sub = subscription();
    sub.follow(9);
    static_cast<void>(sub.push_local(library_event(ApiEventKind::Resync, 1)));
    static_cast<void>(sub.push_outcome(9, op_completed_event(1, 9, std::vector<u8>{})));
    // The engine's copy after delivery is dropped once.
    CHECK(sub.push_from_engine({engine_op_completed(9)}, 1).credit == 1);

    REQUIRE(sub.take().event->kind == static_cast<u32>(ApiEventKind::Resync));
    REQUIRE(sub.take().event->kind == static_cast<u32>(ApiEventKind::OpCompleted));
    CHECK_FALSE(sub.take().event);
}

TEST_CASE("delivery holds across a reconnect in the same epoch and resets on a new one", "[client][events]") {
    EventSubscription sub = subscription();
    sub.follow(9);
    static_cast<void>(sub.push_outcome(9, op_completed_event(1, 9, std::vector<u8>{})));
    sub.set_link(2);
    CHECK(sub.push_from_engine({engine_op_completed(9)}, 2).credit == 1);

    sub.follow(10);
    static_cast<void>(sub.push_outcome(10, op_completed_event(1, 10, std::vector<u8>{})));
    sub.forget_delivered();
    // A new engine's op 10 is another op.
    CHECK(sub.push_from_engine({engine_op_completed(10)}, 2).credit == 0);
}

TEST_CASE("an op is not followed when the filter excludes its OpCompleted or names a session", "[client][events]") {
    EventSubscription only_resync = subscription(ApiEventFilter{{static_cast<u32>(ApiEventKind::Resync)}, {}, {}});
    only_resync.follow(9);
    static_cast<void>(only_resync.push_outcome(9, op_completed_event(1, 9, std::vector<u8>{})));
    CHECK_FALSE(only_resync.take().event);

    EventSubscription other_op = subscription(ApiEventFilter{{}, {}, 8});
    other_op.follow(9);
    static_cast<void>(other_op.push_outcome(9, op_completed_event(1, 9, std::vector<u8>{})));
    CHECK_FALSE(other_op.take().event);

    // The library cannot tell an op's session, so the engine's own event passes through.
    EventSubscription session = subscription(ApiEventFilter{{}, std::vector<u8>{}, {}});
    session.follow(9);
    CHECK(session.push_from_engine({engine_op_completed(9)}, 1).credit == 0);
    CHECK(session.take().event);
}

TEST_CASE("a released op's Outcome is not delivered", "[client][events]") {
    EventSubscription sub = subscription();
    sub.follow(9);
    sub.unfollow(9);
    static_cast<void>(sub.push_outcome(9, op_completed_event(1, 9, std::vector<u8>{})));
    CHECK_FALSE(sub.take().event);
}

TEST_CASE("a closed subscription drains what it holds, then reports closed", "[client][events]") {
    EventSubscription sub = subscription();
    static_cast<void>(sub.push_from_engine({engine_event(1)}, 1));
    sub.close();
    CHECK_FALSE(sub.push_from_engine({engine_event(2)}, 1).wake);

    CHECK(sub.take().event);
    CHECK(sub.take().closed);
    const u64 ticket = sub.open_wait();
    CHECK(sub.take_waiting(ticket).closed);
}

TEST_CASE("an expired wait returns without an event", "[client][events]") {
    EventSubscription sub = subscription();
    const u64 ticket = sub.open_wait();
    sub.expire_wait(ticket);
    const TakenEvent taken = sub.take_waiting(ticket);
    CHECK_FALSE(taken.event);
    CHECK_FALSE(taken.closed);

    // The deadline of a wait that already returned changes nothing.
    sub.expire_wait(ticket);
    static_cast<void>(sub.push_from_engine({engine_event(1)}, 1));
    CHECK(sub.take_waiting(sub.open_wait()).event);
}

TEST_CASE("wait_unused returns once every use is released", "[client][events]") {
    EventSubscription sub = subscription();
    sub.acquire();
    sub.release();
    sub.wait_unused();
    CHECK(sub.id() == 1);
}
