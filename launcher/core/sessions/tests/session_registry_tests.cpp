#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <deque>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

#include "reboot/foundation/random.hpp"
#include "reboot/sessions/session_driver.hpp"
#include "reboot/sessions/session_event.hpp"
#include "reboot/sessions/session_registry.hpp"
#include "reboot/testing/deterministic_runtime.hpp"
#include "reboot/testing/event_recorder.hpp"
#include "reboot/testing/fake_random.hpp"

using namespace reboot;
using namespace reboot::sessions;
using namespace std::chrono_literals;

namespace {

struct DriverLog {
    int stops = 0;
    std::optional<StopRequest> request;
    StopDone done;
    bool destroyed = false;
};

// Keeps `done` outside the driver, so a test can call it after the registry destroyed the driver.
class RecordingDriver final : public ISessionDriver {
public:
    explicit RecordingDriver(DriverLog& log) : log_(log) {}
    ~RecordingDriver() override { log_.destroyed = true; }

    void stop(const StopRequest& request, StopDone done) override {
        ++log_.stops;
        log_.request = request;
        log_.done = std::move(done);
    }

private:
    DriverLog& log_;
};

struct Fixture {
    reboot::testing::DeterministicRuntime runtime;
    reboot::testing::FakeRandom random;
    reboot::testing::EventRecorder recorder{runtime.events()};
    SessionRegistry registry{runtime.clock(), random, runtime.strand(), runtime.timers(), runtime.events()};
    std::deque<DriverLog> logs;

    SessionId open(SessionSpec spec = {}) {
        DriverLog& log = logs.emplace_back();
        auto id = registry.open(std::move(spec), std::make_unique<RecordingDriver>(log));
        REQUIRE(id);
        return *id;
    }

    void finish(std::size_t driver) {
        REQUIRE(logs[driver].done);
        logs[driver].done(Result<void>{});
        runtime.run_until_idle();
    }

    [[nodiscard]] SessionPhase phase(SessionId id) const {
        auto info = registry.get(id);
        REQUIRE(info);
        return info->phase;
    }

    std::vector<const SessionEnded*> ended() {
        recorder.pump();
        return recorder.payloads<SessionEnded>(EventKind::SessionEnded);
    }
};

SessionSpec child_of(SessionId parent) {
    SessionSpec spec;
    spec.kind = SessionKind::Host;
    spec.parent = parent;
    return spec;
}

}  // namespace

TEST_CASE("open lists the session and publishes its state") {
    Fixture f;
    const SessionId id = f.open();
    CHECK(f.registry.has_live());
    REQUIRE(f.registry.list().size() == 1);
    CHECK(f.phase(id) == SessionPhase::Preparing);
    f.recorder.pump();
    CHECK(f.recorder.count(EventKind::SessionStateChanged) == 1);
}

TEST_CASE("stop ends a session once and posts on_ended after SessionEnded") {
    Fixture f;
    const SessionId id = f.open();
    bool notified = false;
    REQUIRE(f.registry.stop(id, StopRequest{.reason = StopReason::User}, [&] { notified = true; }));
    REQUIRE(f.registry.stop(id, StopRequest{.reason = StopReason::BuildRemoved}, nullptr));
    CHECK(f.logs[0].stops == 1);
    CHECK(f.phase(id) == SessionPhase::Stopping);

    f.logs[0].done(Result<void>{});
    CHECK(f.registry.has_live());
    CHECK_FALSE(notified);
    f.runtime.run_until_idle();
    CHECK(notified);
    CHECK_FALSE(f.registry.has_live());
    CHECK(f.logs[0].destroyed);
    const auto ended = f.ended();
    REQUIRE(ended.size() == 1);
    CHECK(ended[0]->reason == StopReason::User);

    bool again = false;
    REQUIRE(f.registry.stop(id, StopRequest{}, [&] { again = true; }));
    CHECK_FALSE(again);
    f.runtime.run_until_idle();
    CHECK(again);
    CHECK(f.registry.get(id).error().id == "sessions.ended");
    CHECK(f.registry.stop(SessionId{uuid_v4(f.random)}, StopRequest{}, nullptr).error().id == "sessions.not_found");
}

TEST_CASE("a parent ends after its linked children") {
    Fixture f;
    const SessionId parent = f.open();
    const SessionId child = f.open(child_of(parent));
    REQUIRE(f.registry.stop(parent, StopRequest{}, nullptr));
    CHECK(f.logs[0].stops == 0);
    REQUIRE(f.logs[1].request);
    CHECK(f.logs[1].request->reason == StopReason::ParentEnded);

    f.finish(1);
    CHECK(f.logs[0].stops == 1);
    f.finish(0);
    const auto ended = f.ended();
    REQUIRE(ended.size() == 2);
    CHECK(ended[0]->session == child);
    CHECK(ended[1]->session == parent);
}

TEST_CASE("only a play session that is starting or running takes a child") {
    Fixture f;
    const SessionId parent = f.open();
    const SessionId host = f.open(child_of(parent));
    CHECK(f.registry.open(child_of(host), std::make_unique<RecordingDriver>(f.logs.emplace_back())).error().id ==
          "sessions.parent_not_live");

    REQUIRE(f.registry.stop(parent, StopRequest{}, nullptr));
    CHECK(f.registry.open(child_of(parent), std::make_unique<RecordingDriver>(f.logs.emplace_back())).error().id ==
          "sessions.parent_not_live");
}

TEST_CASE("a driver that never finishes is ended at grace plus the kill margin") {
    Fixture f;
    const SessionId id = f.open();
    REQUIRE(f.registry.stop(id, StopRequest{.reason = StopReason::User, .grace = 5s}, nullptr));
    f.runtime.advance(5s + kStopKillMargin - 100ms);
    CHECK(f.registry.has_live());

    f.runtime.advance(100ms);
    CHECK_FALSE(f.registry.has_live());
    CHECK(f.logs[0].destroyed);
    auto ended = f.ended();
    REQUIRE(ended.size() == 1);
    REQUIRE(ended[0]->error);
    CHECK(ended[0]->error->id == "internal.bug");

    f.logs[0].done(Result<void>{});
    f.runtime.run_until_idle();
    CHECK(f.ended().size() == 1);
}

TEST_CASE("a self-exit stops the session and a respawned primary's exit is ignored") {
    Fixture f;
    SessionSpec spec;
    spec.kind = SessionKind::Host;
    const SessionId id = f.open(std::move(spec));
    const auto first = f.registry.note_spawned(id, SpawnedProcess{.role = ProcessRole::GameServer, .pid = 10});
    REQUIRE(first);
    CHECK(first->value == 1);

    REQUIRE(f.registry.begin_respawn(id));
    f.registry.report_exit(id, *first, SessionExit{.reason = ExitReason::Crashed});
    CHECK(f.phase(id) == SessionPhase::Preparing);
    CHECK(f.logs[0].stops == 0);

    const auto second = f.registry.note_spawned(id, SpawnedProcess{.role = ProcessRole::GameServer, .pid = 11});
    REQUIRE(second);
    CHECK(second->value == 2);
    f.registry.report_exit(id, *second, SessionExit{.reason = ExitReason::Crashed, .exit_code = 3});
    CHECK(f.phase(id) == SessionPhase::Stopping);
    REQUIRE(f.logs[0].request);
    CHECK(f.logs[0].request->reason == StopReason::Crashed);

    f.finish(0);
    const auto ended = f.ended();
    REQUIRE(ended.size() == 1);
    CHECK(ended[0]->reason == StopReason::Crashed);
    CHECK(ended[0]->exit_code == 3);
}

TEST_CASE("only an active session changes phase or respawns") {
    Fixture f;
    const SessionId id = f.open();
    REQUIRE(f.registry.set_phase(id, SessionPhase::Running));
    CHECK(f.registry.set_phase(id, SessionPhase::Stopping).error().id == "sessions.invalid_transition");

    REQUIRE(f.registry.stop(id, StopRequest{}, nullptr));
    CHECK(f.registry.set_phase(id, SessionPhase::Running).error().id == "sessions.invalid_transition");
    CHECK(f.registry.begin_respawn(id).error().id == "sessions.stopping");
}

TEST_CASE("process exits leave the live list") {
    Fixture f;
    const SessionId id = f.open();
    const SpawnedProcess game{.role = ProcessRole::Game, .space = PidSpace::GuestWindows, .pid = 40};
    REQUIRE(f.registry.note_spawned(id, game));
    f.registry.note_process_exited(id, SpawnedProcess{.role = ProcessRole::Game, .space = PidSpace::Host, .pid = 40});
    CHECK(f.registry.get(id)->processes.size() == 1);
    f.registry.note_process_exited(id, game);
    CHECK(f.registry.get(id)->processes.empty());
}

TEST_CASE("clearing a degraded condition publishes a state change") {
    Fixture f;
    const SessionId id = f.open();
    Diagnostic condition;
    condition.id = "play.backend_slow";
    REQUIRE(f.registry.raise_degraded(id, condition));
    REQUIRE(f.registry.raise_degraded(id, condition));
    CHECK(f.registry.get(id)->degraded.size() == 1);
    f.recorder.pump();
    CHECK(f.recorder.count(EventKind::SessionDegraded) == 2);

    f.recorder.clear();
    REQUIRE(f.registry.clear_degraded(id, "play.backend_slow"));
    REQUIRE(f.registry.clear_degraded(id, "play.backend_slow"));
    f.recorder.pump();
    CHECK(f.recorder.count(EventKind::SessionStateChanged) == 1);
    CHECK(f.registry.get(id)->degraded.empty());
}

TEST_CASE("closing a connection stops only its client-leased sessions") {
    Fixture f;
    SessionSpec leased;
    leased.lease = Lease::client_of(ConnectionId{7});
    const SessionId client = f.open(std::move(leased));
    const SessionId engine = f.open();
    f.registry.on_connection_closed(ConnectionId{7});
    CHECK(f.phase(client) == SessionPhase::Stopping);
    CHECK(f.registry.get(client)->stop_reason == StopReason::LeaseEnded);
    CHECK(f.phase(engine) == SessionPhase::Preparing);
}

TEST_CASE("stop_sessions_using waits for every session pinned to the build") {
    Fixture f;
    const BuildId build{uuid_v4(f.random)};
    SessionSpec pinned;
    pinned.pinned.build = build;
    f.open(pinned);
    f.open(pinned);
    const SessionId other = f.open();
    CHECK(f.registry.sessions_using(build).size() == 2);

    std::optional<Result<void>> result;
    f.registry.stop_sessions_using(build, [&](Result<void> stopped) { result = std::move(stopped); });
    f.finish(0);
    CHECK_FALSE(result);
    f.finish(1);
    REQUIRE(result);
    CHECK(result->has_value());
    CHECK(f.phase(other) == SessionPhase::Preparing);

    result.reset();
    f.registry.stop_sessions_using(build, [&](Result<void> stopped) { result = std::move(stopped); });
    CHECK_FALSE(result);
    f.runtime.run_until_idle();
    CHECK(result);
}

TEST_CASE("refuse_new fails later opens") {
    Fixture f;
    f.registry.refuse_new();
    CHECK(f.registry.open(SessionSpec{}, std::make_unique<RecordingDriver>(f.logs.emplace_back())).error().id ==
          "sessions.refusing_new");
}

TEST_CASE("event_kind follows the alternative") {
    CHECK(event_kind(SessionEvent{SessionSpawned{}}) == EventKind::SessionSpawned);
    CHECK(event_kind(SessionEvent{SessionEnded{}}) == EventKind::SessionEnded);
}
