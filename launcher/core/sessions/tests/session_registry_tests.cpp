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
#include "reboot/sessions/sessions_error.hpp"
#include "reboot/sessions/shutdown_cause.hpp"
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

// Ends inside its own stop(), as a driver with nothing left to kill does.
class InlineDriver final : public ISessionDriver {
public:
    InlineDriver(bool& destroyed, Result<void> outcome) : destroyed_(destroyed), outcome_(std::move(outcome)) {}
    ~InlineDriver() override { destroyed_ = true; }

    void stop(const StopRequest&, StopDone done) override {
        done(std::move(outcome_));
        destroyed_during_stop = destroyed_;
    }

    bool destroyed_during_stop = false;

private:
    bool& destroyed_;
    Result<void> outcome_;
};

// Calls back into the registry from its destructor, as a driver releasing its processes may.
class CallbackDriver final : public ISessionDriver {
public:
    CallbackDriver(SessionRegistry*& registry, SessionId& session) : registry_(registry), session_(session) {}
    ~CallbackDriver() override {
        if (registry_ != nullptr) registry_->note_process_exited(session_, SpawnedProcess{.role = ProcessRole::Game});
    }

    void stop(const StopRequest&, StopDone) override {}

private:
    SessionRegistry*& registry_;
    SessionId& session_;
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
    CHECK(ended[0]->error->id == "sessions.stop_overran");

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

    const auto respawned = f.registry.begin_respawn(id);
    REQUIRE(respawned);
    CHECK(respawned->value == 2);
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

TEST_CASE("a driver that ends inside stop is destroyed only afterwards, and its failure is the session's error") {
    Fixture f;
    bool destroyed = false;
    Diagnostic failed;
    failed.id = "play.kill_failed";
    auto driver = std::make_unique<InlineDriver>(destroyed, std::unexpected(failed));
    InlineDriver* raw = driver.get();
    auto id = f.registry.open(SessionSpec{}, std::move(driver));
    REQUIRE(id);

    Diagnostic launch;
    launch.id = "play.launch_failed";
    REQUIRE(f.registry.stop(*id, StopRequest{.reason = StopReason::LaunchFailed, .error = launch}, nullptr));
    CHECK_FALSE(raw->destroyed_during_stop);
    CHECK_FALSE(destroyed);
    CHECK(f.registry.has_live());

    f.runtime.run_until_idle();
    CHECK(destroyed);
    const auto ended = f.ended();
    REQUIRE(ended.size() == 1);
    CHECK(ended[0]->reason == StopReason::LaunchFailed);
    REQUIRE(ended[0]->error);
    CHECK(ended[0]->error->id == "play.kill_failed");
    REQUIRE(ended[0]->error->causes.size() == 1);
    CHECK(ended[0]->error->causes[0].id == "play.launch_failed");
}

TEST_CASE("a stop request's error is reported in SessionEnded") {
    Fixture f;
    SessionSpec spec;
    spec.kind = SessionKind::Host;
    const SessionId id = f.open(std::move(spec));
    Diagnostic error;
    error.id = "host.listen_failed";
    REQUIRE(f.registry.stop(id, StopRequest{.reason = StopReason::LaunchFailed, .error = error}, nullptr));
    f.finish(0);
    const auto ended = f.ended();
    REQUIRE(ended.size() == 1);
    CHECK(ended[0]->kind == SessionKind::Host);
    REQUIRE(ended[0]->error);
    CHECK(ended[0]->error->id == "host.listen_failed");
}

TEST_CASE("a launch that fails before any spawn ends the session as LaunchFailed") {
    Fixture f;
    const SessionId id = f.open();
    Diagnostic error;
    error.id = "play.spawn_failed";
    f.registry.report_exit(id, Incarnation{}, SessionExit{.reason = ExitReason::LaunchFailed, .error = error});
    REQUIRE(f.logs[0].request);
    CHECK(f.logs[0].request->reason == StopReason::LaunchFailed);
    f.finish(0);
    const auto ended = f.ended();
    REQUIRE(ended.size() == 1);
    CHECK(ended[0]->reason == StopReason::LaunchFailed);
    REQUIRE(ended[0]->error);
    CHECK(ended[0]->error->id == "play.spawn_failed");
}

TEST_CASE("a respawn that fails to launch is reported with the incarnation begin_respawn returned") {
    Fixture f;
    const SessionId id = f.open();
    const auto first = f.registry.note_spawned(id, SpawnedProcess{.role = ProcessRole::Game, .pid = 1});
    REQUIRE(first);
    const auto respawned = f.registry.begin_respawn(id);
    REQUIRE(respawned);
    const auto again = f.registry.begin_respawn(id);
    REQUIRE(again);
    CHECK(*again == *respawned);
    CHECK(*first < *respawned);

    f.registry.report_exit(id, *respawned, SessionExit{.reason = ExitReason::LaunchFailed});
    CHECK(f.phase(id) == SessionPhase::Stopping);
    CHECK(f.registry.get(id)->stop_reason == StopReason::LaunchFailed);
}

TEST_CASE("companions never start an incarnation") {
    Fixture f;
    const SessionId id = f.open();
    const auto game = f.registry.note_spawned(id, SpawnedProcess{.role = ProcessRole::Game, .pid = 1});
    const auto companion = f.registry.note_spawned(id, SpawnedProcess{.role = ProcessRole::Companion, .pid = 2});
    const auto winhost = f.registry.note_spawned(id, SpawnedProcess{.role = ProcessRole::Winhost, .pid = 3});
    REQUIRE(game);
    REQUIRE(companion);
    REQUIRE(winhost);
    CHECK(*companion == *game);
    CHECK(*winhost == *game);
    CHECK(f.registry.get(id)->processes.size() == 3);
    f.recorder.pump();
    CHECK(f.recorder.count(EventKind::SessionSpawned) == 3);
}

TEST_CASE("the first stop reason wins over a later self-exit, whose exit code is still kept") {
    Fixture f;
    const SessionId id = f.open();
    const auto game = f.registry.note_spawned(id, SpawnedProcess{.role = ProcessRole::Game, .pid = 1});
    REQUIRE(game);
    REQUIRE(f.registry.stop(id, StopRequest{.reason = StopReason::User}, nullptr));
    f.registry.report_exit(id, *game, SessionExit{.reason = ExitReason::Crashed, .exit_code = -1073741819});
    f.registry.report_exit(id, *game, SessionExit{.reason = ExitReason::Exited, .exit_code = 0});
    CHECK(f.logs[0].stops == 1);
    f.finish(0);
    const auto ended = f.ended();
    REQUIRE(ended.size() == 1);
    CHECK(ended[0]->reason == StopReason::User);
    CHECK(ended[0]->exit_code == -1073741819);
    CHECK_FALSE(ended[0]->error);
}

TEST_CASE("an ended session takes no change") {
    Fixture f;
    const SessionId id = f.open();
    REQUIRE(f.registry.stop(id, StopRequest{}, nullptr));
    f.finish(0);
    CHECK(f.registry.set_phase(id, SessionPhase::Running).error().id == "sessions.ended");
    CHECK(f.registry.set_lease(id, Lease::engine()).error().id == "sessions.ended");
    CHECK(f.registry.note_spawned(id, SpawnedProcess{}).error().id == "sessions.ended");
    CHECK(f.registry.begin_respawn(id).error().id == "sessions.ended");
    CHECK(f.registry.raise_degraded(id, Diagnostic{}).error().id == "sessions.ended");
    CHECK(f.registry.clear_degraded(id, "x").error().id == "sessions.ended");
    f.registry.report_exit(id, Incarnation{}, SessionExit{});
    f.registry.note_process_exited(id, SpawnedProcess{});
    CHECK(f.registry.list().empty());

    const SessionId unknown{uuid_v4(f.random)};
    CHECK(f.registry.get(unknown).error().id == "sessions.not_found");
    CHECK(f.registry.set_phase(unknown, SessionPhase::Running).error().id == "sessions.not_found");
}

TEST_CASE("set_phase and set_lease publish only a change") {
    Fixture f;
    const SessionId id = f.open();
    f.recorder.pump();
    f.recorder.clear();
    REQUIRE(f.registry.set_phase(id, SessionPhase::Preparing));
    REQUIRE(f.registry.set_lease(id, Lease::engine()));
    f.recorder.pump();
    CHECK(f.recorder.count(EventKind::SessionStateChanged) == 0);

    REQUIRE(f.registry.set_phase(id, SessionPhase::Loading));
    REQUIRE(f.registry.set_phase(id, SessionPhase::Launching));
    REQUIRE(f.registry.set_lease(id, Lease::client_of(ConnectionId{3})));
    f.recorder.pump();
    const auto changes = f.recorder.payloads<SessionStateChanged>(EventKind::SessionStateChanged);
    REQUIRE(changes.size() == 3);
    CHECK(changes[0]->phase == SessionPhase::Loading);
    CHECK(changes[1]->phase == SessionPhase::Launching);
    CHECK(changes[2]->lease == Lease::client_of(ConnectionId{3}));
    CHECK(f.registry.get(id)->lease == Lease::client_of(ConnectionId{3}));

    f.registry.on_connection_closed(ConnectionId{3});
    CHECK(f.registry.get(id)->stop_reason == StopReason::LeaseEnded);
}

TEST_CASE("a lease moved back to the engine survives its connection closing") {
    Fixture f;
    SessionSpec spec;
    spec.lease = Lease::client_of(ConnectionId{4});
    const SessionId id = f.open(std::move(spec));
    REQUIRE(f.registry.set_lease(id, Lease::engine()));
    f.registry.on_connection_closed(ConnectionId{4});
    CHECK(f.phase(id) == SessionPhase::Preparing);
}

TEST_CASE("open records the spec and the open time") {
    Fixture f;
    f.runtime.clock().set_system(std::chrono::system_clock::time_point{std::chrono::hours{100}});
    const SessionId parent = f.open();
    SessionSpec spec = child_of(parent);
    spec.label = "Auto server";
    spec.profile = HostProfileId{uuid_v4(f.random)};
    const SessionId child = f.open(spec);

    const auto info = f.registry.get(child);
    REQUIRE(info);
    CHECK(info->kind == SessionKind::Host);
    CHECK(info->parent == parent);
    CHECK(info->label == "Auto server");
    CHECK(info->profile == spec.profile);
    CHECK(info->started_at == std::chrono::system_clock::time_point{std::chrono::hours{100}});
    CHECK(f.registry.get(parent)->children == std::vector{child});

    const auto listed = f.registry.list();
    REQUIRE(listed.size() == 2);
    CHECK(listed[0].id == parent);
    CHECK(listed[1].id == child);
}

TEST_CASE("a child that ends on its own leaves its parent running") {
    Fixture f;
    const SessionId parent = f.open();
    const SessionId child = f.open(child_of(parent));
    f.registry.report_exit(child, Incarnation{}, SessionExit{.reason = ExitReason::Crashed});
    f.finish(1);
    CHECK(f.phase(parent) == SessionPhase::Preparing);
    CHECK(f.registry.get(parent)->children.empty());

    REQUIRE(f.registry.stop(parent, StopRequest{}, nullptr));
    CHECK(f.logs[0].stops == 1);
}

TEST_CASE("a parent stopped while its child is already stopping keeps the child's reason and waits for it") {
    Fixture f;
    const SessionId parent = f.open();
    const SessionId child = f.open(child_of(parent));
    REQUIRE(f.registry.stop(child, StopRequest{.reason = StopReason::User}, nullptr));
    REQUIRE(f.registry.stop(parent, StopRequest{.reason = StopReason::User}, nullptr));
    CHECK(f.logs[0].stops == 0);
    CHECK(f.logs[1].stops == 1);

    f.finish(1);
    CHECK(f.logs[0].stops == 1);
    f.finish(0);
    const auto ended = f.ended();
    REQUIRE(ended.size() == 2);
    CHECK(ended[0]->session == child);
    CHECK(ended[0]->reason == StopReason::User);
    CHECK(ended[0]->parent == parent);
}

TEST_CASE("a child's overrun delays its parent's stop until the child is ended") {
    Fixture f;
    const SessionId parent = f.open();
    f.open(child_of(parent));
    REQUIRE(f.registry.stop(parent, StopRequest{.reason = StopReason::User, .grace = 2s}, nullptr));
    f.runtime.advance(2s + kStopKillMargin);
    CHECK(f.logs[1].destroyed);
    CHECK(f.logs[0].stops == 1);
    CHECK(f.registry.has_live());
    f.runtime.advance(2s + kStopKillMargin);
    CHECK_FALSE(f.registry.has_live());
}

TEST_CASE("stop_all stops only the kind asked for and posts on_ended once all of them ended") {
    Fixture f;
    const SessionId play = f.open();
    SessionSpec host;
    host.kind = SessionKind::Host;
    const SessionId server = f.open(host);
    const SessionId linked = f.open(child_of(play));

    int notified = 0;
    f.registry.stop_all(SessionKind::Host, StopRequest{.reason = StopReason::EngineShutdown}, [&] { ++notified; });
    CHECK(f.phase(play) == SessionPhase::Preparing);
    CHECK(f.phase(server) == SessionPhase::Stopping);
    CHECK(f.phase(linked) == SessionPhase::Stopping);
    f.finish(1);
    CHECK(notified == 0);
    f.finish(2);
    CHECK(notified == 1);

    f.registry.stop_all(std::nullopt, StopRequest{.reason = StopReason::Update}, [&] { ++notified; });
    f.finish(0);
    CHECK(notified == 2);
    CHECK_FALSE(f.registry.has_live());

    f.registry.stop_all(std::nullopt, StopRequest{}, [&] { ++notified; });
    CHECK(notified == 2);
    f.runtime.run_until_idle();
    CHECK(notified == 3);
}

TEST_CASE("destroying the registry releases every driver and ignores work posted before") {
    reboot::testing::DeterministicRuntime runtime;
    reboot::testing::FakeRandom random;
    auto registry = std::make_unique<SessionRegistry>(runtime.clock(), random, runtime.strand(), runtime.timers(),
                                                      runtime.events());
    SessionRegistry* back = registry.get();
    SessionId first_id;
    REQUIRE(registry->open(SessionSpec{}, std::make_unique<CallbackDriver>(back, first_id)));
    first_id = registry->list()[0].id;
    DriverLog log;
    REQUIRE(registry->open(child_of(first_id), std::make_unique<RecordingDriver>(log)));

    bool notified = false;
    REQUIRE(registry->stop(first_id, StopRequest{}, [&] { notified = true; }));
    REQUIRE(log.done);
    log.done(Result<void>{});
    registry.reset();
    back = nullptr;
    CHECK(log.destroyed);
    runtime.run_until_idle();
    runtime.advance(10s);
    CHECK_FALSE(notified);
}

TEST_CASE("a driver's done that outlives the registry is ignored") {
    reboot::testing::DeterministicRuntime runtime;
    reboot::testing::FakeRandom random;
    auto registry = std::make_unique<SessionRegistry>(runtime.clock(), random, runtime.strand(), runtime.timers(),
                                                      runtime.events());
    DriverLog log;
    auto id = registry->open(SessionSpec{}, std::make_unique<RecordingDriver>(log));
    REQUIRE(id);
    REQUIRE(registry->stop(*id, StopRequest{}, nullptr));
    REQUIRE(log.done);
    runtime.run_until_idle();
    registry.reset();
    CHECK(log.destroyed);
    log.done(Result<void>{});
    CHECK(runtime.run_until_idle() == 0);
}

TEST_CASE("every sessions error names exactly the args its message uses") {
    for (u8 code = 0; code <= static_cast<u8>(SessionsErrorCode::StopOverran); ++code) {
        const Diagnostic diag = to_diagnostic(SessionsError{.code = static_cast<SessionsErrorCode>(code),
                                                            .session = SessionId{},
                                                            .from = SessionPhase::Running,
                                                            .to = SessionPhase::Stopping});
        INFO(diag.id);
        const MessageSpec* spec = nullptr;
        for (const MessageSpec* candidate : message_registry())
            if (candidate->id == diag.id) spec = candidate;
        REQUIRE(spec != nullptr);
        CHECK(diag.domain == ErrorDomain::Sessions);
        CHECK(diag.args.size() == spec->args.size());
        for (const ArgSpec& arg : spec->args) CHECK(diag.find_arg(arg.name) != nullptr);
    }
    CHECK(to_diagnostic(SessionsError{.code = SessionsErrorCode::NotFound}).kind == ErrorKind::NotFound);
}

TEST_CASE("only the stops something asked for count as requested") {
    STATIC_CHECK(was_requested(StopReason::MatchEnded));
    STATIC_CHECK_FALSE(was_requested(StopReason::Exited));
    STATIC_CHECK(stop_reason_name(StopReason::MatchEnded) == "match_ended");
    STATIC_CHECK(stop_reason_for(ExitReason::Unresponsive) == StopReason::Unresponsive);
    STATIC_CHECK(stop_reason_for(ShutdownCause::DrainUpdate) == StopReason::Update);
}
