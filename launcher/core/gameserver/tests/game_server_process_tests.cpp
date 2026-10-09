#include <catch2/catch_test_macros.hpp>
#include <algorithm>
#include <chrono>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "reboot/foundation/paths.hpp"
#include "reboot/gameserver/game_server_process.hpp"
#include "reboot/gameserver/socket_role.hpp"
#include "reboot/testing/fake_game_server.hpp"
#include "reboot/testing/fake_platform_paths.hpp"
#include "reboot/testing/in_memory_file_system.hpp"
#include "reboot/testing/scripted_process_launcher.hpp"
#include "test_strand.hpp"

using namespace rb;
using namespace rb::gameserver;
using namespace std::chrono_literals;
namespace gs = rb::contracts::game_server;

namespace {

constexpr std::string_view kExeName = "reboot-game-server";

const SessionId kSession{*parse_uuid("1c0b8a6e-5f3d-4e2a-9b7c-0d1e2f3a4b5c")};

gs::GameServerDescription test_description() {
    gs::GameServerDescription description = testing::default_fake_description();
    description.capabilities.operator_commands = {"start_match", "end_match", "kick",       "set_bans",
                                                  "set_operators", "run_command", "drain"};
    return description;
}

GameServerConfig test_config() {
    GameServerConfig config;
    config.game.version = *GameVersion::parse("14.40");
    config.game.cl = Changelist{14550713};
    config.listen.ports = {Port{7777}};
    config.match.playlist = "Playlist_DefaultSolo";
    config.operator_cidrs = {"127.0.0.1"};
    return config;
}

template <class T>
std::vector<T> of_type(const std::vector<GameServerEvent>& events) {
    std::vector<T> out;
    for (const GameServerEvent& event : events)
        if (const T* typed = std::get_if<T>(&event)) out.push_back(*typed);
    return out;
}

struct Fixture {
    Fixture() {
        script.bind_sockets = false;
        script.description = test_description();
        launcher.on_exe(kExeName, [this](testing::ScriptedChild& child) -> Result<void> {
            if (spawn_error) return std::unexpected(*spawn_error);
            if (fake) child.attach_peer(std::make_unique<testing::FakeGameServer>(strand, clock, script));
            return {};
        });
    }

    void make(GameServerConfig config = test_config()) {
        GameServerLaunch launch{kSession, DescribedBinary{exe, Sha256Digest{}, script.description}, std::move(config),
                                process::BuiltEnv{}, std::nullopt};
        process = std::make_unique<GameServerProcess>(
            launcher, fs, workers, strand, timers, clock, layout, std::move(launch),
            [this](const GameServerEvent& event) {
                events.push_back(event);
                if (on_event) on_event(event);
            },
            [this](const process::ChildRecord& child, process::RecordChange change) { records.emplace_back(child, change); });
    }

    [[nodiscard]] Result<u32> start() {
        std::optional<Result<u32>> spawned;
        Result<void> started = process->start([&spawned](Result<u32> pid) { spawned = std::move(pid); });
        REQUIRE(started.has_value());
        strand.run_until([&] { return spawned.has_value(); });
        return std::move(*spawned);
    }

    // Started against the fake server and past Listening.
    void listening() {
        make();
        REQUIRE(start().has_value());
        strand.run_until([&] { return process->phase() == GameServerPhase::Listening; });
    }

    [[nodiscard]] testing::ScriptedChild& child() {
        testing::ScriptedChild* last = launcher.last(kExeName);
        REQUIRE(last != nullptr);
        return *last;
    }

    // A hand-driven child: Hello sent and answered.
    void handshaken() {
        fake = false;
        make();
        REQUIRE(start().has_value());
        child().send(gs::ServerHello{script.description});
        strand.run_ready();
        REQUIRE(process->phase() == GameServerPhase::AwaitingListen);
    }

    [[nodiscard]] std::optional<CommandResult> request(OperatorCommand command) {
        std::optional<CommandResult> result;
        Result<void> sent = process->request(std::move(command), [&result](CommandResult answer) { result = std::move(answer); });
        REQUIRE(sent.has_value());
        strand.run_until([&] { return result.has_value(); });
        return result;
    }

    [[nodiscard]] const ServerExited* exited() const {
        if (events.empty()) return nullptr;
        return std::get_if<ServerExited>(&events.back());
    }

    void run_until_exited() {
        strand.run_until([&] { return exited() != nullptr; });
    }

    ManualClock clock;
    test::TestStrand strand{clock};
    TimerService timers{clock, strand};
    testing::InMemoryFileSystem fs;
    testing::ScriptedProcessLauncher launcher{strand, clock, testing::FakeOs::Linux};
    // Declared after what its jobs use, so it joins them before those go away.
    WorkerPool workers{1};
    testing::FakePlatformPaths paths;
    AppLayout layout{DataRoot{testing::default_fake_root() / "data", true}, paths};
    NativePath exe = testing::default_fake_root() / "app" / std::string(kExeName);
    testing::FakeGameServerScript script;
    bool fake = true;
    std::optional<Diagnostic> spawn_error;
    std::vector<GameServerEvent> events;
    UniqueFunction<void(const GameServerEvent&)> on_event;
    std::vector<std::pair<process::ChildRecord, process::RecordChange>> records;
    std::unique_ptr<GameServerProcess> process;
};

}  // namespace

TEST_CASE("start spawns in the session folder, welcomes and reports Listening", "[gameserver][process]") {
    Fixture f;
    f.script.child.hello_delay = 1s;
    f.make();
    CHECK(f.process->phase() == GameServerPhase::Idle);
    CHECK(f.process->session() == kSession);
    std::optional<Result<u32>> spawned;
    REQUIRE(f.process->start([&spawned](Result<u32> pid) { spawned = std::move(pid); }).has_value());
    CHECK(f.process->phase() == GameServerPhase::Preparing);
    f.strand.run_until([&] { return spawned.has_value(); });
    REQUIRE(spawned->has_value());
    CHECK(f.process->phase() == GameServerPhase::Handshaking);
    CHECK(**spawned == f.child().pid());
    CHECK(f.process->pid() == f.child().pid());

    const NativePath dir = f.layout.game_server_session_dir(kSession);
    CHECK(f.fs.is_dir(dir));
    const ports::ProcessLaunch& launch = f.child().launch();
    CHECK(launch.args == std::vector<std::string>{"--control=stdio"});
    CHECK(launch.cwd == dir);
    CHECK(launch.stdio == ports::StdioMode::ControlChannel);
    CHECK(launch.own_group);
    REQUIRE(f.records.size() == 1);
    CHECK(f.records[0].first.role == process::ChildRole::GameServer);
    CHECK(f.records[0].first.session == kSession);
    CHECK(f.records[0].first.ports == std::vector<Port>{Port{7777}});

    CHECK(f.process->bound().empty());
    f.strand.advance(1s);
    f.strand.run_until([&] { return f.process->phase() == GameServerPhase::Listening; });
    const std::optional<Result<gs::ServerWelcome>> welcome = f.child().stdin_frames().last<gs::ServerWelcome>();
    REQUIRE(welcome.has_value());
    REQUIRE(welcome->has_value());
    CHECK((*welcome)->config.session_id == kSession.value);
    CHECK((*welcome)->config.listen.ports == std::vector<u16>{7777});
    CHECK((*welcome)->config.listen.bind_address == "0.0.0.0");
    CHECK((*welcome)->config.log_dir == to_wire(dir));

    const std::vector<Listening> listening = of_type<Listening>(f.events);
    REQUIRE(listening.size() == 1);
    REQUIRE(listening[0].bound.size() == 1);
    CHECK(listening[0].bound[0].role == SocketRole::Game);
    CHECK(listening[0].bound[0].port == 7777);
    REQUIRE(f.process->bound().size() == 1);
    CHECK(f.process->bound()[0].port == 7777);
}

TEST_CASE("start runs from Idle only and validates the config first", "[gameserver][process]") {
    Fixture f;
    GameServerConfig bad = test_config();
    bad.listen.ports = {Port{7777}, Port{7778}};
    f.make(std::move(bad));
    bool called = false;
    Result<void> refused = f.process->start([&called](Result<u32>) { called = true; });
    REQUIRE_FALSE(refused.has_value());
    CHECK(refused.error().id == "gameserver.port_count_mismatch");
    CHECK(f.process->phase() == GameServerPhase::Idle);

    f.make();
    REQUIRE(f.start().has_value());
    Result<void> again = f.process->start([&called](Result<u32>) { called = true; });
    REQUIRE_FALSE(again.has_value());
    CHECK(again.error().id == "gameserver.already_started");
    f.strand.run_ready();
    CHECK_FALSE(called);
    CHECK(f.launcher.children().size() == 1);
}


TEST_CASE("a Hello that differs from the description fails the handshake", "[gameserver][process]") {
    Fixture f;
    f.script.hello_description = test_description();
    f.script.hello_description->build = "other";
    f.make();
    REQUIRE(f.start().has_value());
    f.run_until_exited();
    const ServerExited* exited = f.exited();
    CHECK(exited->cause == process::ChildExitCause::HandshakeFailed);
    REQUIRE(exited->error.has_value());
    CHECK(exited->error->id == "gameserver.description_mismatch");
    CHECK(of_type<Listening>(f.events).empty());
    CHECK(f.child().stdin_frames().count(contract_frame_type_v<gs::ServerWelcome>) == 0);
    CHECK(f.process->phase() == GameServerPhase::Exited);
    REQUIRE(f.records.size() == 2);
    CHECK(f.records[1].second == process::RecordChange::Exited);
    CHECK(f.records[1].first.ports == std::vector<Port>{Port{7777}});
}

TEST_CASE("a Listening that is not the requested block stops the server", "[gameserver][process]") {
    Fixture f;
    f.handshaken();
    std::optional<GameServerPhase> phase_during;
    f.on_event = [&](const GameServerEvent& event) {
        if (std::holds_alternative<ListenFailed>(event)) phase_during = f.process->phase();
    };
    SECTION("another port") {
        f.child().send(gs::Listening{{gs::BoundSocket{gs::SocketRole::Game, 7778}}});
        f.strand.run_ready();
        const std::vector<ListenFailed> failed = of_type<ListenFailed>(f.events);
        REQUIRE(failed.size() == 1);
        CHECK(failed[0].port == Port{7777});
        CHECK(failed[0].bound_instead == Port{7778});
        CHECK(failed[0].stage == ListenStage::Bind);
    }
    SECTION("another role") {
        f.child().send(gs::Listening{{gs::BoundSocket{gs::SocketRole::Beacon, 7777}}});
        f.strand.run_ready();
        const std::vector<ListenFailed> failed = of_type<ListenFailed>(f.events);
        REQUIRE(failed.size() == 1);
        CHECK(failed[0].port == Port{7777});
        CHECK_FALSE(failed[0].bound_instead.has_value());
    }
    SECTION("an extra socket") {
        f.child().send(gs::Listening{{gs::BoundSocket{gs::SocketRole::Game, 7777}, gs::BoundSocket{gs::SocketRole::Beacon, 7778}}});
        f.strand.run_ready();
        const std::vector<ListenFailed> failed = of_type<ListenFailed>(f.events);
        REQUIRE(failed.size() == 1);
        CHECK(failed[0].port == Port{7778});
    }
    SECTION("nothing bound") {
        f.child().send(gs::Listening{});
        f.strand.run_ready();
        const std::vector<ListenFailed> failed = of_type<ListenFailed>(f.events);
        REQUIRE(failed.size() == 1);
        CHECK(failed[0].port == Port{7777});
    }
    CHECK(phase_during == GameServerPhase::Stopping);
    CHECK(f.process->bound().empty());
    CHECK(f.child().stdin_closed());
    f.run_until_exited();
    CHECK(f.child().terminated());
    CHECK(f.exited()->cause == process::ChildExitCause::Requested);
    CHECK_FALSE(f.exited()->error.has_value());
    CHECK(of_type<Listening>(f.events).empty());
    CHECK(f.child().stdin_frames().count(contract_frame_type_v<gs::Shutdown>) == 0);
}

TEST_CASE("a ListenFailed from the server is reported and the server stopped", "[gameserver][process]") {
    Fixture f;
    f.script.bind_failure_index = 0;
    f.make();
    REQUIRE(f.start().has_value());
    f.run_until_exited();
    const std::vector<ListenFailed> failed = of_type<ListenFailed>(f.events);
    REQUIRE(failed.size() == 1);
    CHECK(failed[0].port == Port{7777});
    CHECK(failed[0].stage == ListenStage::Bind);
    REQUIRE(failed[0].os_error.has_value());
    CHECK(failed[0].os_error->code == 10048);
    CHECK(failed[0].os_error->origin == SystemError::Origin::Host);
    CHECK(f.exited()->cause == process::ChildExitCause::Requested);
}

TEST_CASE("operator commands reach the server and are answered once", "[gameserver][process]") {
    Fixture f;
    f.listening();

    std::optional<CommandResult> started = f.request(StartMatch{5s});
    REQUIRE(started.has_value());
    CHECK(started->status == CommandStatus::Ok);
    CHECK_FALSE(started->error.has_value());
    const std::optional<Result<gs::StartMatch>> sent = f.child().stdin_frames().last<gs::StartMatch>();
    REQUIRE(sent.has_value());
    CHECK((*sent)->countdown_s == 5);

    CHECK(f.request(Kick{7, "afk"})->status == CommandStatus::Ok);
    CHECK(f.request(SetBans{{Ban{"10.0.0.0/8", std::nullopt, std::nullopt, "spam"}}})->status == CommandStatus::Ok);
    CHECK(f.request(SetOperators{{"192.168.0.0/16"}})->status == CommandStatus::Ok);
    CHECK(f.request(RunCommand{"startaircraft"})->status == CommandStatus::Ok);
    CHECK(f.request(ResetMatch{})->status == CommandStatus::Ok);
    CHECK(f.request(EndMatch{})->status == CommandStatus::Ok);
    CHECK(f.request(Drain{})->status == CommandStatus::Ok);

    const std::vector<MatchStateChanged> states = of_type<MatchStateChanged>(f.events);
    REQUIRE_FALSE(states.empty());
    CHECK(states.front().state == MatchState::Lobby);
    CHECK(std::ranges::any_of(states, [](const MatchStateChanged& changed) { return changed.state == MatchState::InProgress; }));
    REQUIRE(f.child().stdin_frames().last<gs::Kick>().has_value());
    CHECK((*f.child().stdin_frames().last<gs::Kick>())->player_id == 7);
    CHECK((*f.child().stdin_frames().last<gs::SetBans>())->bans.at(0).address == "10.0.0.0/8");
    CHECK((*f.child().stdin_frames().last<gs::SetOperators>())->ip_cidrs == std::vector<std::string>{"192.168.0.0/16"});
    CHECK((*f.child().stdin_frames().last<gs::RunCommand>())->text == "startaircraft");
}

TEST_CASE("a request is refused up front when it cannot be sent", "[gameserver][process]") {
    Fixture f;
    f.script.description.capabilities = GameServerCapabilities{false, false, {"kick", "set_bans", "set_operators",
                                                                             "start_match"}};
    f.make();
    bool called = false;
    const auto done = [&called](CommandResult) { called = true; };
    Result<void> idle = f.process->request(Kick{1, ""}, done);
    REQUIRE_FALSE(idle.has_value());
    CHECK(idle.error().id == "gameserver.not_running");

    REQUIRE(f.start().has_value());
    f.strand.run_until([&] { return f.process->phase() == GameServerPhase::Listening; });

    Result<void> undeclared = f.process->request(RunCommand{"x"}, done);
    REQUIRE_FALSE(undeclared.has_value());
    CHECK(undeclared.error().id == "gameserver.command_not_declared");
    CHECK(*undeclared.error().find_arg("command") == Arg{std::string("run_command")});
    Result<void> reset = f.process->request(ResetMatch{}, done);
    REQUIRE_FALSE(reset.has_value());
    CHECK(reset.error().id == "gameserver.command_not_declared");

    Result<void> bad_ban = f.process->request(SetBans{{Ban{"not-an-ip", std::nullopt, std::nullopt, ""}}}, done);
    REQUIRE_FALSE(bad_ban.has_value());
    CHECK(bad_ban.error().id == "gameserver.invalid_address");
    Result<void> bad_operator = f.process->request(SetOperators{{"1.2.3.4/40"}}, done);
    REQUIRE_FALSE(bad_operator.has_value());
    CHECK(bad_operator.error().id == "gameserver.invalid_address");
    Result<void> negative = f.process->request(StartMatch{-1s}, done);
    REQUIRE_FALSE(negative.has_value());
    CHECK(negative.error().id == "gameserver.invalid_match_setting");

    f.strand.advance(20s);
    CHECK_FALSE(called);
    CHECK(f.child().stdin_frames().count(contract_frame_type_v<gs::RunCommand>) == 0);
    CHECK(f.child().stdin_frames().count(contract_frame_type_v<gs::Reset>) == 0);
    CHECK(f.child().stdin_frames().count(contract_frame_type_v<gs::SetBans>) == 0);
}

TEST_CASE("an Unsupported answer is CommandStatus::Unsupported", "[gameserver][process]") {
    Fixture f;
    f.script.child.unsupported = {contract_frame_type_v<gs::Drain>};
    f.listening();
    const std::optional<CommandResult> result = f.request(Drain{});
    CHECK(result->status == CommandStatus::Unsupported);
    CHECK_FALSE(result->error.has_value());
}

TEST_CASE("a server error answer is Failed with its diagnostic", "[gameserver][process]") {
    Fixture f;
    f.handshaken();
    std::optional<CommandResult> result;
    REQUIRE(f.process->request(Kick{3, ""}, [&result](CommandResult answer) { result = std::move(answer); }).has_value());
    f.strand.run_ready();
    const std::optional<Result<gs::Kick>> kick = f.child().stdin_frames().last<gs::Kick>();
    REQUIRE(kick.has_value());
    contracts::common::CommandResult refused;
    refused.req_id = (*kick)->req_id;
    refused.ok = false;
    refused.error = contracts::common::to_wire(
        make_diag(ErrorDomain::GameServer, MessageId{"gameserver.test_refused"}).build());
    f.child().send(refused);
    f.strand.run_until([&] { return result.has_value(); });
    CHECK(result->status == CommandStatus::Failed);
    REQUIRE(result->error.has_value());
    CHECK(result->error->id == "gameserver.test_refused");
}

TEST_CASE("an unanswered request times out once and a late reply is ignored", "[gameserver][process]") {
    Fixture f;
    f.handshaken();
    int answers = 0;
    std::optional<CommandResult> result;
    REQUIRE(f.process
                ->request(EndMatch{},
                          [&](CommandResult answer) {
                              ++answers;
                              result = std::move(answer);
                          })
                .has_value());
    f.strand.run_ready();
    f.strand.advance(9s);
    CHECK(answers == 0);
    f.strand.advance(1s);
    REQUIRE(answers == 1);
    CHECK(result->status == CommandStatus::Failed);
    REQUIRE(result->error.has_value());
    CHECK(result->error->id == "gameserver.command_timeout");
    CHECK(*result->error->find_arg("command") == Arg{std::string("end_match")});

    const std::optional<Result<gs::EndMatch>> sent = f.child().stdin_frames().last<gs::EndMatch>();
    REQUIRE(sent.has_value());
    f.child().send(contracts::common::CommandResult{(*sent)->req_id, true, std::nullopt});
    f.strand.run_ready();
    CHECK(answers == 1);
}

TEST_CASE("a request outstanding when the server exits fails with not_running", "[gameserver][process]") {
    Fixture f;
    f.handshaken();
    std::optional<CommandResult> result;
    REQUIRE(f.process->request(StartMatch{}, [&result](CommandResult answer) { result = std::move(answer); }).has_value());
    f.strand.run_ready();
    f.child().exit(ports::ChildExit{70, std::nullopt});
    f.run_until_exited();
    REQUIRE(result.has_value());
    CHECK(result->status == CommandStatus::Failed);
    REQUIRE(result->error.has_value());
    CHECK(result->error->id == "gameserver.not_running");
    CHECK(f.exited()->cause == process::ChildExitCause::Exited);
    REQUIRE(f.exited()->error.has_value());
    CHECK(f.exited()->error->id == "process.child_exited");
    CHECK(f.exited()->status.code == 70);
    CHECK(f.process->phase() == GameServerPhase::Exited);
    CHECK_FALSE(f.process->stop(1s));
}

TEST_CASE("stop while listening sends Shutdown and reports Requested", "[gameserver][process]") {
    Fixture f;
    f.listening();
    CHECK(f.process->stop(3s));
    CHECK(f.process->phase() == GameServerPhase::Stopping);
    CHECK(f.process->stop(3s));
    f.run_until_exited();
    const std::optional<Result<gs::Shutdown>> shutdown = f.child().stdin_frames().last<gs::Shutdown>();
    REQUIRE(shutdown.has_value());
    CHECK((*shutdown)->grace_ms == 3000);
    CHECK(f.child().stdin_frames().count(contract_frame_type_v<gs::Shutdown>) == 1);
    CHECK(f.exited()->cause == process::ChildExitCause::Requested);
    CHECK(f.exited()->status.code == 0);
    CHECK_FALSE(f.exited()->error.has_value());
    CHECK(f.process->phase() == GameServerPhase::Exited);
    CHECK_FALSE(f.child().terminated());
}

TEST_CASE("a server that ignores Shutdown gets stdin closed after the grace, then a kill", "[gameserver][process]") {
    Fixture f;
    f.handshaken();
    REQUIRE(f.process->stop(2s));
    f.strand.run_ready();
    CHECK(f.child().stdin_frames().count(contract_frame_type_v<gs::Shutdown>) == 1);
    f.strand.advance(1900ms);
    CHECK_FALSE(f.child().stdin_closed());
    f.strand.advance(100ms);
    CHECK(f.child().stdin_closed());
    f.strand.advance(4900ms);
    CHECK_FALSE(f.child().terminated());
    f.strand.advance(100ms);
    CHECK(f.child().terminated());
    f.run_until_exited();
    CHECK(f.exited()->cause == process::ChildExitCause::Requested);
}

TEST_CASE("stop while handshaking closes stdin at once and kills after the graceful stop", "[gameserver][process]") {
    Fixture f;
    f.fake = false;
    f.make();
    REQUIRE(f.start().has_value());
    REQUIRE(f.process->phase() == GameServerPhase::Handshaking);
    REQUIRE(f.process->stop(30s));
    CHECK(f.child().stdin_closed());
    f.strand.advance(5s);
    CHECK(f.child().terminated());
    f.run_until_exited();
    CHECK(f.exited()->cause == process::ChildExitCause::Requested);
    CHECK(f.child().stdin_frames().count(contract_frame_type_v<gs::Shutdown>) == 0);
}

TEST_CASE("stop before the spawn cancels it", "[gameserver][process]") {
    Fixture f;
    f.make();
    CHECK_FALSE(f.process->stop(1s));
    std::optional<Result<u32>> spawned;
    REQUIRE(f.process->start([&spawned](Result<u32> pid) { spawned = std::move(pid); }).has_value());
    CHECK(f.process->stop(1s));
    CHECK(f.process->phase() == GameServerPhase::Exited);
    CHECK_FALSE(spawned.has_value());
    f.strand.run_until([&] { return spawned.has_value(); });
    REQUIRE_FALSE(spawned->has_value());
    CHECK(spawned->error().id == "gameserver.stopped_before_start");
    // The one worker runs jobs in order, so once this one answers the folder job has too.
    std::optional<Result<void>> drained;
    f.workers.submit<void>([](CancelToken) -> Result<void> { return {}; }, CancelToken{}, f.strand,
                           [&drained](Result<void> result) { drained = std::move(result); });
    f.strand.run_until([&] { return drained.has_value(); });
    CHECK(f.launcher.children().empty());
    CHECK(f.events.empty());
    CHECK_FALSE(f.process->stop(1s));
}

TEST_CASE("a session folder that cannot be made fails the start", "[gameserver][process]") {
    Fixture f;
    f.fs.faults().fail_next(testing::FsOperation::CreateDirsOwnerOnly,
                            make_diag(ErrorDomain::Posix, MessageId{"posix.test_denied"}).build());
    f.make();
    Result<u32> spawned = f.start();
    REQUIRE_FALSE(spawned.has_value());
    CHECK(spawned.error().id == "gameserver.session_dir_failed");
    REQUIRE(spawned.error().causes.size() == 1);
    CHECK(spawned.error().causes[0].id == "posix.test_denied");
    CHECK(f.process->phase() == GameServerPhase::Exited);
    CHECK(f.launcher.children().empty());
    CHECK(f.events.empty());
}

TEST_CASE("a spawn error goes to spawned and no events follow", "[gameserver][process]") {
    Fixture f;
    f.spawn_error = make_diag(ErrorDomain::Process, MessageId{"process.test_spawn_failed"}).build();
    f.make();
    Result<u32> spawned = f.start();
    REQUIRE_FALSE(spawned.has_value());
    CHECK(spawned.error().id == "process.test_spawn_failed");
    CHECK(f.process->phase() == GameServerPhase::Exited);
    f.strand.advance(30s);
    CHECK(f.events.empty());
    CHECK_FALSE(f.process->pid().has_value());
}

TEST_CASE("match events are translated and the roster follows joins and leaves", "[gameserver][process]") {
    Fixture f;
    f.script.events = {
        testing::TimedServerEvent{1s, gs::PlayerJoined{1, "alice-id", "Alice", "203.0.113.1"}},
        testing::TimedServerEvent{2s, gs::PlayerJoined{2, "bob-id", "Bob", "203.0.113.2"}},
        testing::TimedServerEvent{3s, gs::PlayerCount{2}},
        testing::TimedServerEvent{4s, gs::PlayerLeft{9, "ghost"}},
        testing::TimedServerEvent{5s, gs::PlayerLeft{1, "alice-id"}},
        testing::TimedServerEvent{6s, gs::MatchEnded{gs::MatchEndReason::Completed, std::string("bob-id"),
                                                     {gs::Placement{"bob-id", 1}}}},
        testing::TimedServerEvent{7s, gs::Fatal{"crash", "it broke"}},
    };
    std::vector<std::size_t> roster_during;
    f.on_event = [&](const GameServerEvent& event) {
        if (std::holds_alternative<PlayerJoined>(event) || std::holds_alternative<PlayerLeft>(event))
            roster_during.push_back(f.process->players().size());
    };
    f.listening();
    f.strand.advance(8s);

    const std::vector<PlayerJoined> joined = of_type<PlayerJoined>(f.events);
    REQUIRE(joined.size() == 2);
    CHECK(joined[0].player == Player{1, "alice-id", "Alice", "203.0.113.1"});
    const std::vector<PlayerLeft> left = of_type<PlayerLeft>(f.events);
    REQUIRE(left.size() == 1);
    CHECK(left[0].player == Player{1, "alice-id", "Alice", "203.0.113.1"});
    CHECK(roster_during == std::vector<std::size_t>{0, 1, 2});
    REQUIRE(f.process->players().size() == 1);
    CHECK(f.process->players()[0].display_name == "Bob");

    const std::vector<PlayerCountChanged> counts = of_type<PlayerCountChanged>(f.events);
    REQUIRE(counts.size() == 1);
    CHECK(counts[0].count == 2);
    const std::vector<MatchEnded> ended = of_type<MatchEnded>(f.events);
    REQUIRE(ended.size() == 1);
    CHECK(ended[0].reason == MatchEndReason::Completed);
    CHECK(ended[0].winner == std::optional<std::string>("bob-id"));
    REQUIRE(ended[0].placements.size() == 1);
    const std::vector<ServerFatal> fatal = of_type<ServerFatal>(f.events);
    REQUIRE(fatal.size() == 1);
    CHECK(fatal[0].code == "crash");
    CHECK(fatal[0].detail == "it broke");
    // Fatal is only reported; the host policy decides.
    CHECK(f.process->phase() == GameServerPhase::Listening);
}

TEST_CASE("missed Pongs are reported without stopping the server", "[gameserver][process]") {
    Fixture f;
    f.handshaken();
    f.strand.advance(19s);
    CHECK(of_type<ServerUnresponsive>(f.events).empty());
    f.strand.advance(1s);
    const std::vector<ServerUnresponsive> unresponsive = of_type<ServerUnresponsive>(f.events);
    REQUIRE(unresponsive.size() == 1);
    CHECK(unresponsive[0].missed == 3);
    CHECK(f.process->phase() == GameServerPhase::AwaitingListen);
    CHECK_FALSE(f.child().terminated());
}

TEST_CASE("destroying the process kills the child with no further events", "[gameserver][process]") {
    Fixture f;
    f.listening();
    bool answered = false;
    REQUIRE(f.process->request(Drain{}, [&answered](CommandResult) { answered = true; }).has_value());
    const std::size_t before = f.events.size();
    testing::ScriptedChild& child = f.child();
    f.process.reset();
    f.strand.advance(30s);
    CHECK(child.terminated());
    CHECK(f.events.size() == before);
    CHECK_FALSE(answered);
    REQUIRE(f.records.size() == 2);
    CHECK(f.records[1].second == process::RecordChange::Exited);
}

TEST_CASE("a sink may destroy the process on ServerExited", "[gameserver][process]") {
    Fixture f;
    bool saw_exit = false;
    f.on_event = [&](const GameServerEvent& event) {
        if (!std::holds_alternative<ServerExited>(event)) return;
        saw_exit = true;
        f.process.reset();
    };
    f.listening();
    REQUIRE(f.process->stop(1s));
    f.strand.run_until([&] { return saw_exit; });
    CHECK(f.process == nullptr);
    f.strand.advance(10s);
}

TEST_CASE("a sink or a command callback may destroy the process in the middle of a delivery", "[gameserver][process]") {
    Fixture f;
    f.handshaken();
    testing::ScriptedChild& child = f.child();
    const auto chunk_of = [](std::vector<u8> first, const std::vector<u8>& second) {
        first.insert(first.end(), second.begin(), second.end());
        return first;
    };
    SECTION("from an event with more frames behind it") {
        f.on_event = [&](const GameServerEvent& event) {
            if (std::holds_alternative<PlayerJoined>(event)) f.process.reset();
        };
        child.write_stdout(chunk_of(encode_contract_frame(gs::PlayerJoined{1, "a", "A", "203.0.113.1"}),
                                    encode_contract_frame(gs::PlayerJoined{2, "b", "B", "203.0.113.2"})));
        f.strand.run_ready();
        CHECK(f.process == nullptr);
        CHECK(of_type<PlayerJoined>(f.events).size() == 1);
    }
    SECTION("from a command answer with an event behind it") {
        bool answered = false;
        REQUIRE(f.process
                    ->request(Kick{3, ""},
                              [&](CommandResult) {
                                  answered = true;
                                  f.process.reset();
                              })
                    .has_value());
        f.strand.run_ready();
        const std::optional<Result<gs::Kick>> kick = child.stdin_frames().last<gs::Kick>();
        REQUIRE(kick.has_value());
        child.write_stdout(chunk_of(encode_contract_frame(contracts::common::CommandResult{(*kick)->req_id, true, std::nullopt}),
                                    encode_contract_frame(gs::PlayerCount{1})));
        f.strand.run_ready();
        CHECK(answered);
        CHECK(f.process == nullptr);
        CHECK(of_type<PlayerCountChanged>(f.events).empty());
    }
    SECTION("from a command that the exit fails") {
        std::optional<CommandResult> result;
        REQUIRE(f.process
                    ->request(EndMatch{},
                              [&](CommandResult answer) {
                                  result = std::move(answer);
                                  f.process.reset();
                              })
                    .has_value());
        f.strand.run_ready();
        child.exit(ports::ChildExit{1, std::nullopt});
        f.strand.run_ready();
        REQUIRE(result.has_value());
        CHECK(result->error->id == "gameserver.not_running");
        CHECK(f.process == nullptr);
        CHECK(f.exited() == nullptr);
    }
    f.strand.advance(10s);
    CHECK((child.terminated() || child.exited()));
    REQUIRE(f.records.size() == 2);
    CHECK(f.records[1].second == process::RecordChange::Exited);
}

TEST_CASE("events with enum values outside the contract are dropped", "[gameserver][process]") {
    Fixture f;
    f.handshaken();
    f.child().send(gs::StateChanged{static_cast<gs::MatchState>(9)});
    f.child().send(gs::MatchEnded{static_cast<gs::MatchEndReason>(9), std::nullopt, {}});
    f.child().send(gs::StateChanged{gs::MatchState::Warmup});
    f.strand.run_ready();
    const std::vector<MatchStateChanged> states = of_type<MatchStateChanged>(f.events);
    REQUIRE(states.size() == 1);
    CHECK(states[0].state == MatchState::Warmup);
    CHECK(of_type<MatchEnded>(f.events).empty());
    CHECK(f.process->phase() == GameServerPhase::AwaitingListen);
}
