#include <algorithm>
#include <chrono>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#include <boost/asio/executor_work_guard.hpp>
#include <boost/asio/io_context.hpp>

#include <catch2/catch_message.hpp>
#include <catch2/catch_test_macros.hpp>

#include "reboot/contracts/backend.hpp"
#include "reboot/contracts/common.hpp"
#include "reboot/contracts/game_server.hpp"
#include "reboot/ports/process.hpp"
#include "reboot/testing/contract_conformance.hpp"
#include "reboot/testing/deterministic_runtime.hpp"
#include "reboot/testing/fake_backend.hpp"
#include "reboot/testing/fake_game_server.hpp"
#include "reboot/testing/fake_platform_paths.hpp"
#include "reboot/testing/frame_log.hpp"
#include "reboot/testing/manual_waiter.hpp"
#include "reboot/testing/scripted_process_launcher.hpp"

using namespace reboot;
using namespace reboot::testing;
using namespace std::chrono_literals;
namespace be = contracts::backend;
namespace gs = contracts::game_server;
namespace common = contracts::common;

namespace {

// An io_context on its own thread, for the fakes' real sockets.
struct IoThread {
    boost::asio::io_context io;
    boost::asio::executor_work_guard<boost::asio::io_context::executor_type> work = boost::asio::make_work_guard(io);
    std::thread thread{[this] { io.run(); }};

    ~IoThread() {
        work.reset();
        io.stop();
        thread.join();
    }
};

// The engine's side of one spawned child.
struct Child {
    std::unique_ptr<ports::ChildProcess> process;
    std::shared_ptr<FrameLog> frames = std::make_shared<FrameLog>(kChildFrameCap);
    std::shared_ptr<std::string> err = std::make_shared<std::string>();
    std::shared_ptr<std::optional<ports::ChildExit>> exit = std::make_shared<std::optional<ports::ChildExit>>();

    explicit Child(std::unique_ptr<ports::ChildProcess> child) : process(std::move(child)) {
        process->on_stdout([frames = frames](std::span<const u8> bytes) { (void)frames->feed(bytes); });
        process->on_stderr([err = err](std::span<const u8> bytes) { err->append(bytes.begin(), bytes.end()); });
        process->on_exit([exit = exit](ports::ChildExit e) { *exit = e; });
    }

    template <ContractMessage T>
    void send(const T& message) {
        process->write_stdin(encode_contract_frame(message));
    }
};

[[nodiscard]] ports::ProcessLaunch launch_of(std::string_view exe, std::string mode = "--control=stdio") {
    ports::ProcessLaunch launch;
    launch.exe = default_fake_root() / "app" / std::string(exe);
    launch.args = {std::move(mode)};
    launch.stdio = ports::StdioMode::ControlChannel;
    return launch;
}

// What `reboot-fake-game-server --describe` does, in-process.
class DescribePeer final : public IStdioPeer {
public:
    explicit DescribePeer(FakeGameServerScript script) : script_(std::move(script)) {}
    void start(StdioPeerOutputs outputs) override {
        outputs.stdout_bytes(describe_frame(script_));
        outputs.exit(0);
    }
    void on_stdin(std::span<const u8>) override {}
    void on_stdin_eof() override {}

private:
    FakeGameServerScript script_;
};

// The suite reached `check` and it held, so a pass is not an early return.
[[nodiscard]] bool ran(const ConformanceReport& report, std::string_view check) {
    return std::ranges::any_of(report.checks(), [&](const ConformanceCheck& c) {
        return c.name == check && c.status == CheckStatus::Passed;
    });
}

[[nodiscard]] gs::ServerConfig config_with(std::vector<u16> ports) {
    gs::ServerConfig config;
    config.listen = gs::ListenConfig{"127.0.0.1", std::move(ports)};
    return config;
}

}  // namespace

TEST_CASE("FakeBackend passes the backend contract in-process", "[testing][conformance][peers]") {
    DeterministicRuntime runtime;
    ManualWaiter waiter(runtime);
    IoThread http;
    ScriptedProcessLauncher launcher(runtime.strand(), runtime.clock(), FakeOs::Windows);
    FakeBackendScript script;
    script.serve_http = true;
    launcher.serve_exe("reboot-backend.exe", [&](const ports::ProcessLaunch&) {
        return std::make_unique<FakeBackend>(runtime.strand(), runtime.clock(), script, http.io);
    });
    ContractConformance conformance(launcher, waiter);
    const ContractSubject subject{default_fake_root() / "app" / "reboot-backend.exe", {}, {},
                                  default_fake_root() / "missing-parent" / "backend"};
    const ConformanceReport report = conformance.run_backend(subject);
    INFO(report.describe());
    CHECK(report.passed());
    CHECK(ran(report, "Ready names a serving HTTP listener that answers backend-info"));
    CHECK(ran(report, "backend-info names a Reboot backend whose WebSocket shares the HTTP port"));
    CHECK(ran(report, "every request gets exactly one reply"));
    CHECK(ran(report, "credentials are unique and not empty"));
    CHECK(ran(report, "a bind failure exits non-zero"));
}

TEST_CASE("FakeGameServer passes the game server contract in-process", "[testing][conformance][peers]") {
    DeterministicRuntime runtime;
    ManualWaiter waiter(runtime);
    IoThread sockets;
    ScriptedProcessLauncher launcher(runtime.strand(), runtime.clock(), FakeOs::Linux);
    FakeGameServerScript script;
    script.description.sockets.push_back(gs::SocketSpec{gs::SocketRole::Beacon});
    launcher.serve_exe("reboot-game-server", [&](const ports::ProcessLaunch& launch) -> std::unique_ptr<IStdioPeer> {
        if (std::ranges::contains(launch.args, std::string("--describe"))) return std::make_unique<DescribePeer>(script);
        return std::make_unique<FakeGameServer>(runtime.strand(), runtime.clock(), script, sockets.io);
    });
    ContractConformance conformance(launcher, waiter);
    const ContractSubject subject{default_fake_root() / "app" / "reboot-game-server", {}, {}, default_fake_root() / "gs"};
    const ConformanceReport report = conformance.run_game_server(subject, {gs::GameSpec{"12.41", 1, std::nullopt}, Port{27917}});
    INFO(report.describe());
    CHECK(report.passed());
    CHECK(ran(report, "Listening reports exactly the Welcome ports"));
    CHECK(ran(report, "the game port answers the rbsb probe"));
    CHECK(ran(report, "Reset keeps the block"));
    CHECK(ran(report, "an occupied port gives ListenFailed for it"));
    CHECK(ran(report, "stdin EOF ends the server within the grace"));
}

TEST_CASE("FakeBackend serves requests, records the engine's answers and replays nothing itself", "[testing][peers]") {
    DeterministicRuntime runtime;
    ScriptedProcessLauncher launcher(runtime.strand(), runtime.clock(), FakeOs::Windows);
    FakeBackendScript script;
    script.content = {3, 99};
    script.match_target_requests = {be::ResolveMatchTarget{77, "player", "playlist_defaultsolo"}};
    script.logins = {be::LoginObserved{"player", "key"}};
    script.child.stderr_lines = {"backend starting"};
    runtime.clock().set_system(std::chrono::system_clock::time_point{std::chrono::hours{24 * 365 * 50}});
    FakeBackend* backend = nullptr;
    launcher.serve_exe("reboot-backend.exe", [&](const ports::ProcessLaunch&) {
        auto made = std::make_unique<FakeBackend>(runtime.strand(), runtime.clock(), script);
        backend = made.get();
        return made;
    });
    auto spawned = launcher.spawn(launch_of("reboot-backend.exe"));
    REQUIRE(spawned);
    Child child(std::move(*spawned));
    runtime.run_until_idle();
    REQUIRE(child.frames->last<be::BackendHello>());
    CHECK(child.frames->last<be::BackendHello>()->value().content.serial == 99);
    CHECK(*child.err == "backend starting\n");

    child.send(be::BackendWelcome{"127.0.0.1", {}, LogLevel::Info});
    runtime.run_until_idle();
    CHECK(child.frames->last<be::Ready>()->value().http_port == 1);
    CHECK(child.frames->last<be::ResolveMatchTarget>()->value().req_id == 77);
    CHECK(child.frames->count(contract_frame_type_v<be::LoginObserved>) == 1);

    child.send(be::RegisterAccount{1, "player", {}, be::AccountRole::Client});
    child.send(be::RegisterAccount{2, "host", {}, be::AccountRole::Host});
    child.send(be::RenameAccount{3, "player", "host", be::RenameConflictPolicy::Report});
    child.send(be::ConfigureSession{4, "key", "player", "http://127.0.0.1:1/s/key/", "console", {"12.41", 1}});
    child.send(be::MatchTarget{77, "10.0.0.2:7777", std::nullopt});
    child.send(be::MintLaunchCredential{5, "player", be::CredentialKind::LaunchSecret, 1});
    child.send(be::AccountsList{8});
    runtime.run_until_idle();
    const auto listed = child.frames->last<be::AccountsReply>();
    REQUIRE(listed);
    REQUIRE(*listed);
    const auto& summaries = (*listed)->accounts;
    REQUIRE(summaries.size() == 2);
    // Only the scripted login has a last login, which prune compares with its cut-off.
    const u64 logged_in = summaries[0].account_id == "player" ? summaries[0].last_login_unix_ms : summaries[1].last_login_unix_ms;
    const u64 never = summaries[0].account_id == "host" ? summaries[0].last_login_unix_ms : summaries[1].last_login_unix_ms;
    CHECK(logged_in > 0);
    CHECK(never == 0);
    child.send(be::AccountsPrune{9, logged_in, std::nullopt});
    child.send(be::AccountsPrune{10, ~u64{0}, be::AccountRole::Host});
    runtime.run_until_idle();
    CHECK(backend->registered_accounts().size() == 2);
    child.send(be::AccountsPrune{6, logged_in + 1, std::nullopt});
    child.send(be::AccountsList{7});
    runtime.run_until_idle();
    REQUIRE(backend != nullptr);
    REQUIRE(backend->registered_accounts().size() == 1);
    CHECK(backend->registered_accounts().front().account_id == "host");
    CHECK(backend->live_sessions().size() == 1);
    CHECK(backend->credentials_minted() == 1);
    CHECK(backend->match_targets().front().endpoint == "10.0.0.2:7777");
    CHECK(backend->welcome()->bind_address == "127.0.0.1");
    CHECK(child.frames->count(contract_frame_type_v<be::AccountRenameConflict>) == 1);
    const auto results = child.frames->all<common::CommandResult>();
    REQUIRE(results);
    CHECK(std::ranges::any_of(*results, [](const common::CommandResult& r) {
        return r.req_id == 3 && !r.ok && r.error && r.error->id == "testing.rename_conflict";
    }));
    CHECK(child.frames->last<be::AccountsReply>()->value().accounts.size() == 1);

    child.process->close_stdin();
    runtime.run_until_idle();
    REQUIRE(*child.exit);
    CHECK((*child.exit)->code == 0);
}

TEST_CASE("FakeBackend misbehaves as scripted", "[testing][peers]") {
    DeterministicRuntime runtime;
    ScriptedProcessLauncher launcher(runtime.strand(), runtime.clock(), FakeOs::Windows);
    FakeBackendScript script;
    script.child.protocol = 999;
    script.child.garbage_frame = contract_frame_type_v<be::Ready>;
    script.child.unsupported = {contract_frame_type_v<be::Health>};
    script.child.reply_delay = 2s;
    script.child.hang = ScriptedFailure{ScriptStage::Ready, 10s};
    script.child.crash = ScriptedFailure{ScriptStage::Ready, 20s};
    script.child.ignore_stdin_eof = true;
    launcher.serve_exe("reboot-backend.exe",
                       [&](const ports::ProcessLaunch&) { return std::make_unique<FakeBackend>(runtime.strand(), runtime.clock(), script); });
    auto spawned = launcher.spawn(launch_of("reboot-backend.exe"));
    REQUIRE(spawned);
    Child child(std::move(*spawned));
    runtime.run_until_idle();
    CHECK(child.frames->last<be::BackendHello>()->value().protocol == 999);
    const auto garbage = child.frames->last<be::Ready>();
    REQUIRE(garbage);
    CHECK_FALSE(*garbage);

    child.send(be::BackendWelcome{"127.0.0.1", {}, LogLevel::Info});
    child.send(be::Health{1});
    child.send(be::Drain{2});
    runtime.run_until_idle();
    CHECK(child.frames->count(contract_frame_type_v<common::Unsupported>) == 0);
    runtime.advance(2s);
    CHECK(child.frames->last<common::Unsupported>()->value().req_id == 1);
    CHECK(child.frames->last<common::CommandResult>()->value().req_id == 2);

    child.send(common::Ping{1});
    runtime.run_until_idle();
    CHECK(child.frames->count(contract_frame_type_v<common::Pong>) == 1);

    runtime.advance(10s);
    child.send(common::Ping{2});
    runtime.run_until_idle();
    CHECK(child.frames->count(contract_frame_type_v<common::Pong>) == 1);
    child.process->close_stdin();
    runtime.run_until_idle();
    CHECK_FALSE(*child.exit);
    runtime.advance(10s);
    REQUIRE(*child.exit);
    CHECK((*child.exit)->code == 70);
}

TEST_CASE("a FakeBackend scripted to fail its bind exits before Ready", "[testing][peers]") {
    DeterministicRuntime runtime;
    ScriptedProcessLauncher launcher(runtime.strand(), runtime.clock(), FakeOs::Windows);
    FakeBackendScript script;
    script.bind_failure = true;
    launcher.serve_exe("reboot-backend.exe",
                       [&](const ports::ProcessLaunch&) { return std::make_unique<FakeBackend>(runtime.strand(), runtime.clock(), script); });
    Child child(std::move(*launcher.spawn(launch_of("reboot-backend.exe"))));
    child.send(be::BackendWelcome{"127.0.0.1", {}, LogLevel::Info});
    runtime.run_until_idle();
    REQUIRE(*child.exit);
    CHECK((*child.exit)->code == kBindFailureExitCode);
    CHECK(child.frames->count(contract_frame_type_v<be::Ready>) == 0);
}

TEST_CASE("FakeGameServer runs a match, resets and records operator requests", "[testing][peers]") {
    DeterministicRuntime runtime;
    ScriptedProcessLauncher launcher(runtime.strand(), runtime.clock(), FakeOs::Linux);
    FakeGameServerScript script;
    script.bind_sockets = false;
    script.match_length = 30s;
    script.events = {TimedServerEvent{5s, gs::PlayerJoined{1, "player", "Player", "10.0.0.3"}},
                     TimedServerEvent{6s, gs::PlayerCount{1}}};
    script.hello_description = default_fake_description();
    script.hello_description->build = "other";
    FakeGameServer* server = nullptr;
    std::optional<gs::Listening> listened;
    launcher.serve_exe("reboot-game-server", [&](const ports::ProcessLaunch&) {
        auto made = std::make_unique<FakeGameServer>(runtime.strand(), runtime.clock(), script);
        made->on_listening([&listened](const gs::Listening& listening) { listened = listening; });
        server = made.get();
        return made;
    });
    Child child(std::move(*launcher.spawn(launch_of("reboot-game-server"))));
    runtime.run_until_idle();
    CHECK(child.frames->last<gs::ServerHello>()->value().description.build == "other");

    child.send(gs::ServerWelcome{config_with({7777})});
    runtime.run_until_idle();
    REQUIRE(listened);
    CHECK(listened->bound.front().port == 7777);
    CHECK(server->state() == gs::MatchState::Lobby);
    runtime.advance(6s);
    CHECK(child.frames->count(contract_frame_type_v<gs::PlayerJoined>) == 1);
    CHECK(child.frames->count(contract_frame_type_v<gs::PlayerCount>) == 1);

    child.send(gs::StartMatch{1, 0});
    child.send(gs::Kick{2, 1, "afk"});
    child.send(gs::SetOperators{3, {"10.0.0.0/8"}});
    child.send(gs::RunCommand{4, "startsafezone"});
    child.send(gs::SetBans{5, {gs::Ban{"10.0.0.9", std::nullopt, std::nullopt, "cheating"}}});
    runtime.run_until_idle();
    CHECK(server->state() == gs::MatchState::InProgress);
    CHECK(server->kicked() == std::vector<u32>{1});
    CHECK(server->operator_cidrs() == std::vector<std::string>{"10.0.0.0/8"});
    CHECK(server->commands() == std::vector<std::string>{"startsafezone"});
    CHECK(server->bans().size() == 1);

    runtime.advance(30s);
    CHECK(server->state() == gs::MatchState::Ending);
    CHECK(child.frames->last<gs::MatchEnded>()->value().reason == gs::MatchEndReason::Completed);

    child.send(gs::Reset{6});
    child.send(gs::StartMatch{7, 0});
    child.send(gs::EndMatch{8});
    runtime.run_until_idle();
    CHECK(child.frames->all<gs::MatchEnded>()->size() == 2);
    CHECK(child.frames->last<gs::MatchEnded>()->value().reason == gs::MatchEndReason::Operator);

    child.send(gs::Shutdown{9, 1000});
    runtime.run_until_idle();
    REQUIRE(*child.exit);
    CHECK((*child.exit)->code == 0);
}

TEST_CASE("FakeGameServer reports a scripted ListenFailed and a bad block", "[testing][peers]") {
    DeterministicRuntime runtime;
    ScriptedProcessLauncher launcher(runtime.strand(), runtime.clock(), FakeOs::Linux);
    FakeGameServerScript script;
    script.bind_sockets = false;
    script.bind_failure_index = 0;
    launcher.serve_exe("reboot-game-server",
                       [&](const ports::ProcessLaunch&) { return std::make_unique<FakeGameServer>(runtime.strand(), runtime.clock(), script); });
    Child failing(std::move(*launcher.spawn(launch_of("reboot-game-server"))));
    failing.send(gs::ServerWelcome{config_with({7777})});
    runtime.run_until_idle();
    CHECK(failing.frames->last<gs::ListenFailed>()->value().port == 7777);
    CHECK((*failing.exit)->code == kBindFailureExitCode);

    script.bind_failure_index.reset();
    Child miscounted(std::move(*launcher.spawn(launch_of("reboot-game-server"))));
    miscounted.send(gs::ServerWelcome{config_with({7777, 7778})});
    runtime.run_until_idle();
    CHECK(miscounted.frames->last<gs::Fatal>()->value().code == "bad_config");
    CHECK((*miscounted.exit)->code == kBadInvocationExitCode);
}

TEST_CASE("describe_frame is the description alone", "[testing][peers]") {
    FakeGameServerScript script;
    FrameLog log(kChildFrameCap);
    REQUIRE(log.feed(describe_frame(script)));
    const auto described = log.all<gs::GameServerDescription>();
    REQUIRE(described);
    REQUIRE(described->size() == 1);
    CHECK(described->front().build == "fake");
    CHECK(described->front().capabilities.in_process_reset);
    CHECK(described->front().sockets.size() == 1);
}
