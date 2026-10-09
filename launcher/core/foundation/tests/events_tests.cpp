#include <any>
#include <cstddef>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "reboot/foundation/events.hpp"

using namespace rb;

namespace {

struct Payload {
    int tag = 0;
    std::size_t bytes = 10;

    [[nodiscard]] std::size_t approx_bytes() const noexcept { return bytes; }
};

int tag_of(const EventEnvelope& event) { return std::any_cast<const Payload&>(event.payload).tag; }

std::vector<EventEnvelope> drain_all(Subscription& subscription) {
    std::vector<EventEnvelope> out;
    subscription.drain(out, 1000);
    return out;
}

const SessionId kSession{Uuid{{1}}};

}  // namespace

TEST_CASE("Delivery classes follow the event kind", "[foundation][events]") {
    CHECK(delivery_class(EventKind::OpProgress) == DeliveryClass::CoalesceLatest);
    CHECK(delivery_class(EventKind::LogLine) == DeliveryClass::DropWithCounter);
    CHECK(delivery_class(EventKind::OpCompleted) == DeliveryClass::NeverDrop);
    CHECK(delivery_class(EventKind::SessionEnded) == DeliveryClass::NeverDrop);
    CHECK(delivery_class(EventKind::UserActionRequired) == DeliveryClass::NeverDrop);
}

TEST_CASE("Events carry the bus epoch and a seq that grows across kinds", "[foundation][events]") {
    EventBus bus(EngineEpoch{7});
    const auto all = bus.subscribe({}, 1 << 20);
    bus.publish(EventKind::NoticeAdded, Payload{1});
    bus.publish(EventKind::SessionEnded, Payload{2}, EventScope{kSession, OpId{3}, {}});
    const std::vector<EventEnvelope> events = drain_all(*all);
    REQUIRE(events.size() == 2);
    CHECK(events[0].epoch == EngineEpoch{7});
    CHECK(events[0].seq == 1);
    CHECK(events[1].seq == 2);
    CHECK(events[1].session == kSession);
    CHECK(events[1].op == OpId{3});
    CHECK(events[1].approx_bytes == 10);
    CHECK(tag_of(events[1]) == 2);
}

TEST_CASE("Filters select by kind, session and op", "[foundation][events]") {
    EventBus bus(EngineEpoch{1});
    const auto by_kind = bus.subscribe(EventFilter{{EventKind::SessionEnded}, {}, {}}, 1 << 20);
    const auto by_session = bus.subscribe(EventFilter{{}, kSession, {}}, 1 << 20);
    const auto by_op = bus.subscribe(EventFilter{{}, {}, OpId{9}}, 1 << 20);
    bus.publish(EventKind::NoticeAdded, Payload{1});
    bus.publish(EventKind::SessionEnded, Payload{2}, EventScope{kSession, {}, {}});
    bus.publish(EventKind::OpCompleted, Payload{3}, EventScope{{}, OpId{9}, {}});
    CHECK(drain_all(*by_kind).size() == 1);
    const auto session_events = drain_all(*by_session);
    REQUIRE(session_events.size() == 1);
    CHECK(tag_of(session_events[0]) == 2);
    const auto op_events = drain_all(*by_op);
    REQUIRE(op_events.size() == 1);
    CHECK(tag_of(op_events[0]) == 3);
}

TEST_CASE("CoalesceLatest keeps the newest event per key, at the back", "[foundation][events]") {
    EventBus bus(EngineEpoch{1});
    const auto sub = bus.subscribe({}, 1 << 20);
    bus.publish(EventKind::OpProgress, Payload{1}, EventScope{{}, OpId{1}, "1"});
    bus.publish(EventKind::OpProgress, Payload{2}, EventScope{{}, OpId{2}, "2"});
    bus.publish(EventKind::NoticeAdded, Payload{3});
    bus.publish(EventKind::OpProgress, Payload{4}, EventScope{{}, OpId{1}, "1"});
    const auto events = drain_all(*sub);
    REQUIRE(events.size() == 3);
    CHECK(tag_of(events[0]) == 2);
    CHECK(tag_of(events[1]) == 3);
    CHECK(tag_of(events[2]) == 4);
    CHECK(events[1].seq < events[2].seq);
}

TEST_CASE("Notify fires on empty to non-empty and on a new Resync only", "[foundation][events]") {
    EventBus bus(EngineEpoch{1});
    const auto sub = bus.subscribe({}, 25);
    int notified = 0;
    sub->set_notify([&] { ++notified; });
    bus.publish(EventKind::OpProgress, Payload{1}, EventScope{{}, OpId{1}, "1"});
    bus.publish(EventKind::OpProgress, Payload{2}, EventScope{{}, OpId{1}, "1"});
    CHECK(notified == 1);
    bus.publish(EventKind::NoticeAdded, Payload{3});
    CHECK(notified == 1);
    // 20 queued, 10 more does not fit: Resync.
    bus.publish(EventKind::SessionEnded, Payload{4});
    CHECK(notified == 2);
    CHECK(sub->resync_pending());
    bus.publish(EventKind::SessionEnded, Payload{5, 30});
    CHECK(notified == 2);
}

TEST_CASE("A NeverDrop event that cannot fit clears the queue and asks for one Resync",
          "[foundation][events][resync]") {
    EventBus bus(EngineEpoch{1});
    const auto sub = bus.subscribe({}, 30);
    bus.publish(EventKind::NoticeAdded, Payload{1});
    bus.publish(EventKind::NoticeAdded, Payload{2});
    bus.publish(EventKind::NoticeAdded, Payload{3});
    CHECK_FALSE(sub->resync_pending());
    bus.publish(EventKind::OpCompleted, Payload{4});
    CHECK(sub->resync_pending());
    CHECK(drain_all(*sub).empty());
    CHECK(sub->take_resync());
    CHECK_FALSE(sub->take_resync());

    // After the reader re-fetched, delivery resumes.
    bus.publish(EventKind::OpCompleted, Payload{5});
    const auto events = drain_all(*sub);
    REQUIRE(events.size() == 1);
    CHECK(tag_of(events[0]) == 5);
}

TEST_CASE("Lossy events make room for NeverDrop events and are counted", "[foundation][events]") {
    EventBus bus(EngineEpoch{1});
    const auto sub = bus.subscribe({}, 30);
    bus.publish(EventKind::LogLine, Payload{1});
    bus.publish(EventKind::NoticeAdded, Payload{2});
    bus.publish(EventKind::LogLine, Payload{3});
    // A full queue drops a new LogLine.
    bus.publish(EventKind::LogLine, Payload{4});
    CHECK(sub->dropped() == 1);
    bus.publish(EventKind::SessionEnded, Payload{5});
    CHECK(sub->dropped() == 2);
    CHECK_FALSE(sub->resync_pending());
    const auto events = drain_all(*sub);
    REQUIRE(events.size() == 3);
    CHECK(tag_of(events[0]) == 2);
    CHECK(tag_of(events[1]) == 3);
    CHECK(tag_of(events[2]) == 5);
}

TEST_CASE("drain honours max and frees budget", "[foundation][events]") {
    EventBus bus(EngineEpoch{1});
    const auto sub = bus.subscribe({}, 20);
    bus.publish(EventKind::NoticeAdded, Payload{1});
    bus.publish(EventKind::NoticeAdded, Payload{2});
    std::vector<EventEnvelope> out;
    CHECK(sub->drain(out, 1) == 1);
    bus.publish(EventKind::NoticeAdded, Payload{3});
    CHECK_FALSE(sub->resync_pending());
    CHECK(sub->drain(out, 10) == 2);
    REQUIRE(out.size() == 3);
    CHECK(tag_of(out[2]) == 3);
}

TEST_CASE("A dropped subscription stops receiving and a notify may subscribe", "[foundation][events]") {
    EventBus bus(EngineEpoch{1});
    auto first = bus.subscribe({}, 1 << 20);
    std::shared_ptr<Subscription> late;
    first->set_notify([&] {
        if (!late) late = bus.subscribe({}, 1 << 20);
    });
    bus.publish(EventKind::NoticeAdded, Payload{1});
    REQUIRE(late);
    // Subscribed during the dispatch of event 1, so it sees only later events.
    CHECK(drain_all(*late).empty());
    first.reset();
    bus.publish(EventKind::NoticeAdded, Payload{2});
    const auto events = drain_all(*late);
    REQUIRE(events.size() == 1);
    CHECK(tag_of(events[0]) == 2);
}

TEST_CASE("An event published from a notify callback reaches every queue after the one that woke it",
          "[foundation][events]") {
    EventBus bus(EngineEpoch{1});
    const auto first = bus.subscribe({}, 1 << 20);
    const auto second = bus.subscribe({}, 1 << 20);
    bool republished = false;
    first->set_notify([&] {
        if (std::exchange(republished, true)) return;
        bus.publish(EventKind::NoticeAdded, Payload{2});
    });
    bus.publish(EventKind::NoticeAdded, Payload{1});

    for (const auto& subscription : {first, second}) {
        const std::vector<EventEnvelope> events = drain_all(*subscription);
        REQUIRE(events.size() == 2);
        CHECK(tag_of(events[0]) == 1);
        CHECK(tag_of(events[1]) == 2);
        CHECK(events[0].seq < events[1].seq);
    }
}
