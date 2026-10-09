#include <any>
#include <chrono>
#include <memory>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "reboot/foundation/clock.hpp"
#include "reboot/foundation/events.hpp"
#include "reboot/foundation/executor.hpp"
#include "reboot/foundation/operation.hpp"

using namespace rb;
using namespace std::chrono_literals;

namespace {

struct Rig {
    ManualClock clock;
    ManualExecutor strand{clock};
    TimerService timers{clock, strand};
    EventBus events{EngineEpoch{1}};
    OpRegistry ops{clock, timers, events};
    std::shared_ptr<Subscription> sub = events.subscribe({}, 1 << 20);
    std::vector<OpCompletedEvent> completions;

    // Drains the subscription, keeping completions aside, and returns the progress events.
    std::vector<OpProgressEvent> progress() {
        std::vector<EventEnvelope> out;
        sub->drain(out, 1000);
        std::vector<OpProgressEvent> found;
        for (const EventEnvelope& event : out) {
            if (event.kind == EventKind::OpProgress) found.push_back(std::any_cast<OpProgressEvent>(event.payload));
            else if (event.kind == EventKind::OpCompleted)
                completions.push_back(std::any_cast<OpCompletedEvent>(event.payload));
        }
        return found;
    }

    std::vector<OpCompletedEvent> completed() {
        progress();
        return std::exchange(completions, {});
    }
};

template <class A>
bool holds(const std::optional<ErasedOutcome>& outcome) {
    return outcome && std::holds_alternative<A>(*outcome);
}

constexpr ConnectionId kConn1{1};
constexpr ConnectionId kConn2{2};

}  // namespace

TEST_CASE("An op completes exactly once and publishes one OpCompleted", "[foundation][operation]") {
    Rig rig;
    auto [handle, op] = rig.ops.create<int>(OpKind::Generic, DisconnectPolicy::Detached, std::nullopt);
    CHECK(handle.id() == op.id());
    CHECK_FALSE(op.done());
    CHECK(rig.ops.has_live_detached());

    CHECK(op.complete(Completed<int>{5}));
    CHECK(op.done());
    CHECK_FALSE(op.complete(Failed{internal_bug("test")}));
    CHECK_FALSE(rig.ops.has_live_detached());

    const auto outcome = rig.ops.outcome(handle.id());
    REQUIRE(holds<Completed<std::any>>(outcome));
    CHECK(std::any_cast<int>(std::get<Completed<std::any>>(*outcome).value) == 5);
    const auto completed = rig.completed();
    REQUIRE(completed.size() == 1);
    CHECK(completed[0].op == handle.id());
    CHECK(completed[0].kind == OpKind::Generic);
}

TEST_CASE("Completed<void> erases to an empty any", "[foundation][operation]") {
    Rig rig;
    auto [handle, op] = rig.ops.create<void>(OpKind::Generic, DisconnectPolicy::Detached, std::nullopt);
    CHECK(op.complete(Completed<void>{}));
    const auto outcome = rig.ops.outcome(handle.id());
    REQUIRE(holds<Completed<std::any>>(outcome));
    CHECK_FALSE(std::get<Completed<std::any>>(*outcome).value.has_value());
}

TEST_CASE("The deadline times out with the last phase and cancels the work", "[foundation][operation][deadline]") {
    Rig rig;
    auto [handle, op] = rig.ops.create<int>(OpKind::HttpSmall, DisconnectPolicy::Detached, std::nullopt);
    Operation<int>& work = op;
    std::optional<CancelReason> heard;
    bool done_when_heard = false;
    bool late_complete = true;
    const CancelRegistration registration = op.token().on_cancel([&](CancelReason reason) {
        heard = reason;
        done_when_heard = work.done();
        late_complete = work.complete(Completed<int>{1});
    });
    op.progress(Progress{"connecting"});
    rig.strand.advance(19'999ms);
    CHECK_FALSE(op.done());
    rig.strand.advance(1ms);
    CHECK(heard == CancelReason::Deadline);
    // The outcome was decided before the work heard of it.
    CHECK(done_when_heard);
    CHECK_FALSE(late_complete);
    const auto outcome = rig.ops.outcome(handle.id());
    REQUIRE(holds<TimedOut>(outcome));
    CHECK(std::get<TimedOut>(*outcome).phase == "connecting");
    CHECK(rig.completed().size() == 1);
}

TEST_CASE("Deadlines scale with the runner multiplier and accept an override", "[foundation][operation][deadline]") {
    Rig rig;
    auto [wine, wine_op] = rig.ops.create<int>(OpKind::GameControlHello, DisconnectPolicy::Detached, std::nullopt,
                                               RunnerMultiplier::Wine);
    // An override is scaled too.
    auto [custom, custom_op] = rig.ops.create<int>(OpKind::HttpSmall, DisconnectPolicy::Detached, std::nullopt,
                                                   RunnerMultiplier::RosettaFirstRun, 500ms);
    rig.strand.advance(1'999ms);
    CHECK_FALSE(custom_op.done());
    rig.strand.advance(1ms);
    CHECK(holds<TimedOut>(rig.ops.outcome(custom.id())));
    rig.strand.advance(1'999ms);
    CHECK_FALSE(wine_op.done());
    rig.strand.advance(1ms);
    CHECK(holds<TimedOut>(rig.ops.outcome(wine.id())));
}

TEST_CASE("Progress re-arms a liveness deadline but not a total one", "[foundation][operation][deadline]") {
    Rig rig;
    auto [install, install_op] = rig.ops.create<int>(OpKind::Install, DisconnectPolicy::Detached, std::nullopt);
    auto [dns, dns_op] = rig.ops.create<int>(OpKind::Dns, DisconnectPolicy::Detached, std::nullopt);
    for (u64 i = 0; i < 5; ++i) {
        rig.strand.advance(100s);
        install_op.progress(Progress{"downloading", i});
        dns_op.progress(Progress{"resolving"});
    }
    CHECK_FALSE(install_op.done());
    CHECK(holds<TimedOut>(rig.ops.outcome(dns.id())));
    rig.strand.advance(120s);
    const auto outcome = rig.ops.outcome(install.id());
    REQUIRE(holds<TimedOut>(outcome));
    CHECK(std::get<TimedOut>(*outcome).phase == "downloading");
}

TEST_CASE("AwaitingUser suspends a total deadline and keeps what was left of it", "[foundation][operation][deadline]") {
    Rig rig;
    auto [handle, op] = rig.ops.create<int>(OpKind::BackendReady, DisconnectPolicy::Detached, std::nullopt);
    rig.strand.advance(10s);
    op.awaiting_user(RequestId{4});
    rig.strand.advance(1h);
    CHECK_FALSE(op.done());
    const auto live = rig.ops.live();
    REQUIRE(live.size() == 1);
    REQUIRE(live[0].progress);
    CHECK(live[0].progress->awaiting_user == RequestId{4});
    const auto published = rig.progress();
    REQUIRE(published.size() == 1);
    CHECK(published[0].awaiting_user == RequestId{4});

    op.progress(Progress{"starting"});
    CHECK_FALSE(rig.ops.live()[0].progress->awaiting_user);
    rig.strand.advance(19'999ms);
    CHECK_FALSE(op.done());
    rig.strand.advance(1ms);
    CHECK(holds<TimedOut>(rig.ops.outcome(handle.id())));
}

TEST_CASE("AwaitingUser can still be cancelled", "[foundation][operation]") {
    Rig rig;
    auto [handle, op] = rig.ops.create<int>(OpKind::Play, DisconnectPolicy::Detached, std::nullopt);
    op.awaiting_user(RequestId{1});
    REQUIRE(rig.ops.cancel(handle.id(), CancelReason::User));
    const auto outcome = rig.ops.outcome(handle.id());
    REQUIRE(holds<Cancelled>(outcome));
    CHECK(std::get<Cancelled>(*outcome).reason == CancelReason::User);
    CHECK(op.token().reason() == CancelReason::User);
}

TEST_CASE("Progress is coalesced to 10 Hz with the newest values", "[foundation][operation][progress]") {
    Rig rig;
    auto [handle, op] = rig.ops.create<int>(OpKind::Install, DisconnectPolicy::Detached, std::nullopt);
    op.progress(Progress{"download", 1, 100});
    op.progress(Progress{"download", 2, 100});
    rig.strand.advance(50ms);
    op.progress(Progress{"extract", 3, 100, 7, 2s});
    auto published = rig.progress();
    REQUIRE(published.size() == 1);
    CHECK(published[0].done == 1);
    CHECK(published[0].op == handle.id());
    CHECK(published[0].kind == OpKind::Install);

    rig.strand.advance(49ms);
    CHECK(rig.progress().empty());
    rig.strand.advance(1ms);
    published = rig.progress();
    REQUIRE(published.size() == 1);
    CHECK(published[0].phase == "extract");
    CHECK(published[0].done == 3);
    CHECK(published[0].total == u64{100});
    CHECK(published[0].rate_per_s == u64{7});
    CHECK(published[0].eta == std::chrono::seconds{2});

    // Quiet for longer than the interval: the next one goes out at once.
    rig.strand.advance(1s);
    op.progress(Progress{"extract", 4});
    CHECK(rig.progress().size() == 1);
}

TEST_CASE("Completion drops a pending coalesced progress", "[foundation][operation][progress]") {
    Rig rig;
    auto [handle, op] = rig.ops.create<int>(OpKind::Install, DisconnectPolicy::Detached, std::nullopt);
    op.progress(Progress{"a", 1});
    op.progress(Progress{"a", 2});
    CHECK(op.complete(Completed<int>{0}));
    op.progress(Progress{"a", 3});
    rig.strand.advance(1s);
    CHECK(rig.progress().size() == 1);
    CHECK(rig.ops.live().empty());
}

TEST_CASE("cancel is idempotent and unknown ops are not found", "[foundation][operation]") {
    Rig rig;
    auto [handle, op] = rig.ops.create<int>(OpKind::Generic, DisconnectPolicy::Detached, std::nullopt);
    CHECK(rig.ops.cancel(handle.id(), CancelReason::Shutdown));
    CHECK(rig.ops.cancel(handle.id(), CancelReason::User));
    const auto outcome = rig.ops.outcome(handle.id());
    REQUIRE(holds<Cancelled>(outcome));
    CHECK(std::get<Cancelled>(*outcome).reason == CancelReason::Shutdown);
    CHECK(op.token().reason() == CancelReason::Shutdown);
    CHECK(rig.completed().size() == 1);

    const Result<void> missing = rig.ops.cancel(OpId{999}, CancelReason::User);
    REQUIRE_FALSE(missing);
    CHECK(missing.error().id == "foundation.op_not_found");
    CHECK(missing.error().kind == ErrorKind::NotFound);
    CHECK_FALSE(rig.ops.attach(OpId{999}, kConn1));
}

TEST_CASE("Complete versus cancel versus deadline: the first one wins", "[foundation][operation][race]") {
    SECTION("complete, then cancel and deadline") {
        Rig rig;
        auto [handle, op] = rig.ops.create<int>(OpKind::Dns, DisconnectPolicy::Detached, std::nullopt);
        rig.strand.advance(4'999ms);
        CHECK(op.complete(Completed<int>{1}));
        CHECK(rig.ops.cancel(handle.id(), CancelReason::User));
        rig.strand.advance(1s);
        CHECK(holds<Completed<std::any>>(rig.ops.outcome(handle.id())));
        CHECK_FALSE(op.token().cancelled());
        CHECK(rig.completed().size() == 1);
    }
    SECTION("cancel, then the work completes from its cancel callback, then the deadline") {
        Rig rig;
        auto [handle, op] = rig.ops.create<int>(OpKind::Dns, DisconnectPolicy::Detached, std::nullopt);
        Operation<int>& work = op;
        bool late_complete = true;
        const CancelRegistration registration =
            op.token().on_cancel([&](CancelReason) { late_complete = work.complete(Completed<int>{1}); });
        CHECK(rig.ops.cancel(handle.id(), CancelReason::Superseded));
        CHECK_FALSE(late_complete);
        rig.strand.advance(1min);
        const auto outcome = rig.ops.outcome(handle.id());
        REQUIRE(holds<Cancelled>(outcome));
        CHECK(std::get<Cancelled>(*outcome).reason == CancelReason::Superseded);
        CHECK(rig.completed().size() == 1);
    }
    SECTION("deadline, then complete and cancel") {
        Rig rig;
        auto [handle, op] = rig.ops.create<int>(OpKind::Dns, DisconnectPolicy::Detached, std::nullopt);
        rig.strand.advance(5s);
        CHECK_FALSE(op.complete(Completed<int>{1}));
        CHECK(rig.ops.cancel(handle.id(), CancelReason::User));
        CHECK(holds<TimedOut>(rig.ops.outcome(handle.id())));
        CHECK(op.token().reason() == CancelReason::Deadline);
        CHECK(rig.completed().size() == 1);
    }
}

TEST_CASE("A released outcome goes at once; an unreleased one after 10 minutes",
          "[foundation][operation][retention]") {
    Rig rig;
    auto [attached, attached_op] = rig.ops.create<int>(OpKind::Generic, DisconnectPolicy::Detached, std::nullopt);
    auto [loose, loose_op] = rig.ops.create<int>(OpKind::Generic, DisconnectPolicy::Detached, std::nullopt);
    REQUIRE(rig.ops.attach(attached.id(), kConn1));
    REQUIRE(rig.ops.attach(attached.id(), kConn2));
    CHECK(attached_op.complete(Completed<int>{1}));
    CHECK(loose_op.complete(Completed<int>{2}));

    rig.ops.release(attached.id(), kConn1);
    CHECK(rig.ops.outcome(attached.id()));
    rig.ops.release(attached.id(), kConn2);
    CHECK_FALSE(rig.ops.outcome(attached.id()));
    CHECK_FALSE(rig.ops.attach(attached.id(), kConn1));

    rig.strand.advance(9min);
    CHECK(rig.ops.outcome(loose.id()));
    rig.strand.advance(1min);
    CHECK_FALSE(rig.ops.outcome(loose.id()));
}

TEST_CASE("A release before completion keeps the outcome for its retention", "[foundation][operation][retention]") {
    Rig rig;
    auto [handle, op] = rig.ops.create<int>(OpKind::Install, DisconnectPolicy::Detached, std::nullopt);
    REQUIRE(rig.ops.attach(handle.id(), kConn1));
    rig.ops.release(handle.id(), kConn1);
    CHECK(op.complete(Completed<int>{1}));
    CHECK(rig.ops.outcome(handle.id()));
}

TEST_CASE("Closing a connection cancels only its BoundToConnection ops", "[foundation][operation]") {
    Rig rig;
    auto [bound, bound_op] = rig.ops.create<int>(OpKind::Generic, DisconnectPolicy::BoundToConnection, std::nullopt);
    auto [detached, detached_op] = rig.ops.create<int>(OpKind::Generic, DisconnectPolicy::Detached, std::nullopt);
    auto [other, other_op] = rig.ops.create<int>(OpKind::Generic, DisconnectPolicy::BoundToConnection, std::nullopt);
    REQUIRE(rig.ops.attach(bound.id(), kConn1));
    REQUIRE(rig.ops.attach(detached.id(), kConn1));
    REQUIRE(rig.ops.attach(other.id(), kConn2));

    rig.ops.on_connection_closed(kConn1);
    CHECK(bound_op.token().reason() == CancelReason::Disconnect);
    // Its only attachment went with the connection, so the outcome went too.
    CHECK_FALSE(rig.ops.outcome(bound.id()));
    CHECK_FALSE(detached_op.done());
    CHECK_FALSE(other_op.done());
    CHECK(rig.ops.has_live_detached());

    const auto live = rig.ops.live();
    REQUIRE(live.size() == 2);
    CHECK(live[0].op == detached.id());
    CHECK(live[0].policy == DisconnectPolicy::Detached);
    CHECK_FALSE(live[0].progress);
    CHECK(live[1].op == other.id());
}

TEST_CASE("The work's Operation reference outlives its released outcome", "[foundation][operation][lifetime]") {
    Rig rig;
    auto [handle, op] = rig.ops.create<int>(OpKind::Generic, DisconnectPolicy::BoundToConnection, std::nullopt);
    REQUIRE(rig.ops.attach(handle.id(), kConn1));
    rig.ops.on_connection_closed(kConn1);
    CHECK_FALSE(rig.ops.outcome(handle.id()));

    // The work notices later and still reaches its object.
    CHECK(op.done());
    op.progress(Progress{"late"});
    CHECK_FALSE(op.complete(Completed<int>{1}));
    CHECK(rig.completed().size() == 1);

    // A later retirement sweeps it; new ops are unaffected.
    auto [next, next_op] = rig.ops.create<int>(OpKind::Generic, DisconnectPolicy::Detached, std::nullopt);
    CHECK(next_op.complete(Completed<int>{2}));
    REQUIRE(rig.ops.attach(next.id(), kConn2));
    rig.ops.release(next.id(), kConn2);
    CHECK_FALSE(rig.ops.outcome(next.id()));
}

TEST_CASE("A release from inside the OpCompleted dispatch is safe", "[foundation][operation][lifetime]") {
    Rig rig;
    auto [handle, op] = rig.ops.create<int>(OpKind::Generic, DisconnectPolicy::Detached, std::nullopt);
    REQUIRE(rig.ops.attach(handle.id(), kConn1));
    const auto sub = rig.events.subscribe(EventFilter{{EventKind::OpCompleted}, {}, {}}, 1 << 20);
    const OpId id = handle.id();
    sub->set_notify([&rig, id] { rig.ops.release(id, kConn1); });
    CHECK(op.complete(Completed<int>{1}));
    CHECK(op.done());
    CHECK_FALSE(rig.ops.outcome(handle.id()));
}

TEST_CASE("A cancel from inside the OpCompleted dispatch changes nothing", "[foundation][operation][race]") {
    Rig rig;
    auto [handle, op] = rig.ops.create<int>(OpKind::Generic, DisconnectPolicy::Detached, std::nullopt);
    const auto sub = rig.events.subscribe(EventFilter{{EventKind::OpCompleted}, {}, {}}, 1 << 20);
    const OpId id = handle.id();
    bool cancel_ok = false;
    sub->set_notify([&rig, &cancel_ok, id] { cancel_ok = rig.ops.cancel(id, CancelReason::User).has_value(); });
    CHECK(op.complete(Completed<int>{1}));
    CHECK(cancel_ok);
    CHECK(holds<Completed<std::any>>(rig.ops.outcome(id)));
    CHECK_FALSE(op.token().cancelled());
    CHECK(rig.completed().size() == 1);
}

TEST_CASE("Default deadlines follow the table and are never infinite", "[foundation][operation][deadline]") {
    CHECK(default_deadline(OpKind::HttpSmall) == 20s);
    CHECK(default_deadline(OpKind::GameControlHello) == 2s);
    CHECK(default_deadline(OpKind::ShutdownBudget) == 30s);
    CHECK(default_deadline(OpKind::Install) == 120s);
    CHECK(uses_liveness_deadline(OpKind::Install));
    CHECK_FALSE(uses_liveness_deadline(OpKind::HostReadiness));
    CHECK(scaled(2s, RunnerMultiplier::RosettaFirstRun) == 8s);
}
