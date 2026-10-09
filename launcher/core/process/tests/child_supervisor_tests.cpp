#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "reboot/contracts/backend.hpp"
#include "reboot/contracts/common.hpp"
#include "reboot/process/child_observer.hpp"
#include "reboot/process/child_request_handler.hpp"
#include "reboot/process/child_supervisor.hpp"
#include "reboot/testing/deterministic_runtime.hpp"
#include "reboot/testing/fake_backend.hpp"
#include "reboot/testing/fake_platform_paths.hpp"
#include "reboot/testing/scripted_process_launcher.hpp"

using namespace reboot;
using namespace reboot::process;
using namespace std::chrono_literals;

namespace be = reboot::contracts::backend;
namespace common = reboot::contracts::common;

namespace {

constexpr std::string_view kExe = "reboot-backend";

struct Observer final : ChildObserver {
    void on_running(u32 generation) override { running.push_back(generation); }
    void on_event(const RawFrame& frame) override { events.push_back(frame.type); }
    void on_unresponsive(u32 missed) override { unresponsive.push_back(missed); }
    void on_exit(const ChildExitInfo& exit) override { exits.push_back(exit); }

    std::vector<u32> running;
    std::vector<u64> events;
    std::vector<u32> unresponsive;
    std::vector<ChildExitInfo> exits;
};

struct Handler final : ChildRequestHandler {
    [[nodiscard]] bool handles(u64 frame_type) const override {
        return frame_type == contract_frame_type_v<be::ResolveMatchTarget>;
    }
    void on_request(const RawFrame& frame, ChildReply reply) override {
        Result<be::ResolveMatchTarget> request = decode_contract<be::ResolveMatchTarget>(frame.payload);
        REQUIRE(request.has_value());
        accounts.push_back(request->account_id);
        replies.push_back(std::move(reply));
    }

    std::vector<std::string> accounts;
    std::vector<ChildReply> replies;
};

ChildHandshake backend_handshake() {
    return make_child_handshake<be::BackendHello>(
        be::kBackendProtocol, [](const be::BackendHello& hello) { return hello.protocol; },
        [](const be::BackendHello&) -> Result<be::BackendWelcome> { return be::BackendWelcome{"127.0.0.1", {}, {}}; });
}

ProcessSpec backend_spec() {
    ProcessSpec spec;
    spec.role = ChildRole::Backend;
    spec.session = SessionId{};
    spec.exe = testing::default_fake_root() / "bin" / std::string(kExe);
    spec.args = {"--control=stdio"};
    return spec;
}

RestartPolicy quick_restarts() {
    return RestartPolicy{.first_delay = 1s, .max_delay = 4s, .max_restarts = 2, .window = 1min};
}

struct Fixture {
    explicit Fixture(std::optional<RestartPolicy> restart = std::nullopt, bool with_handler = true,
                     ProcessSpec spec = backend_spec()) {
        launcher.on_exe(kExe, [this](testing::ScriptedChild& child) -> Result<void> {
            if (fail_spawns > 0) {
                --fail_spawns;
                return make_diag(ErrorDomain::Process, MessageId{"process.test_spawn_failed"}).fail();
            }
            if (fake) child.attach_peer(std::make_unique<testing::FakeBackend>(rt.strand(), rt.clock(), *fake));
            return {};
        });
        ChildSupervisorOptions options;
        options.hello_timeout = 2s;
        options.liveness = LivenessPolicy{.interval = 1s, .miss_limit = 3};
        options.restart = restart;
        auto record = [this](const ChildRecord& child, RecordChange change) { records.emplace_back(child, change); };
        if (with_handler)
            supervisor = std::make_unique<ChildSupervisor>(launcher, rt.strand(), rt.timers(), rt.clock(), std::move(spec),
                                                           backend_handshake(), std::move(options), handler, observer,
                                                           record);
        else
            supervisor = std::make_unique<ChildSupervisor>(launcher, rt.strand(), rt.timers(), rt.clock(), std::move(spec),
                                                           backend_handshake(), std::move(options), observer, record);
    }

    [[nodiscard]] testing::ScriptedChild& child() {
        testing::ScriptedChild* last = launcher.last(kExe);
        REQUIRE(last != nullptr);
        return *last;
    }

    void hello(u32 protocol = be::kBackendProtocol) {
        child().send(be::BackendHello{protocol, "1.0", {}});
        rt.run_until_idle();
    }

    // Started and past the handshake.
    void run() {
        REQUIRE(supervisor->start().has_value());
        hello();
        REQUIRE(supervisor->state() == ChildState::Running);
    }

    template <ContractMessage T>
    [[nodiscard]] T last_sent() {
        std::optional<Result<T>> sent = child().stdin_frames().last<T>();
        REQUIRE(sent.has_value());
        REQUIRE(sent->has_value());
        return **sent;
    }

    testing::DeterministicRuntime rt;
    testing::ScriptedProcessLauncher launcher{rt.strand(), rt.clock(), testing::FakeOs::Linux};
    int fail_spawns = 0;
    // Set: each spawn runs a FakeBackend with this script.
    std::optional<testing::FakeBackendScript> fake;
    Observer observer;
    Handler handler;
    std::vector<std::pair<ChildRecord, RecordChange>> records;
    std::unique_ptr<ChildSupervisor> supervisor;
};

const ports::ChildExit kSigKill{std::nullopt, 9};

}  // namespace

TEST_CASE("start spawns the spec in its own group and the Hello gets the Welcome", "[process][supervisor]") {
    Fixture f;
    REQUIRE(f.supervisor->start().has_value());
    CHECK(f.supervisor->state() == ChildState::Starting);
    CHECK(f.supervisor->generation() == 1);

    const ports::ProcessLaunch& launch = f.child().launch();
    CHECK(launch.own_group);
    CHECK(launch.stdio == ports::StdioMode::ControlChannel);
    CHECK(launch.cwd == f.child().launch().exe.parent_path());
    CHECK(launch.args == std::vector<std::string>{"--control=stdio"});
    REQUIRE(f.records.size() == 1);
    CHECK(f.records[0].second == RecordChange::Spawned);
    CHECK(f.records[0].first.pid == f.child().pid());
    CHECK(f.records[0].first.created == f.child().created());
    CHECK(f.records[0].first.role == ChildRole::Backend);
    CHECK(f.supervisor->pid() == f.child().pid());

    f.hello();
    CHECK(f.supervisor->state() == ChildState::Running);
    CHECK(f.observer.running == std::vector<u32>{1});
    CHECK(f.last_sent<be::BackendWelcome>().bind_address == "127.0.0.1");
}

TEST_CASE("start refuses while a child is live", "[process][supervisor]") {
    Fixture f;
    f.run();
    Result<void> again = f.supervisor->start();
    REQUIRE_FALSE(again.has_value());
    CHECK(again.error().id == "process.child_already_started");
    CHECK(f.launcher.children().size() == 1);
}

TEST_CASE("a spawn error is returned by start with no on_exit", "[process][supervisor]") {
    Fixture f(quick_restarts());
    f.fail_spawns = 1;
    Result<void> started = f.supervisor->start();
    REQUIRE_FALSE(started.has_value());
    CHECK(started.error().id == "process.test_spawn_failed");
    f.rt.advance(10s);
    CHECK(f.observer.exits.empty());
    CHECK(f.supervisor->state() == ChildState::Failed);
    CHECK(f.records.empty());

    REQUIRE(f.supervisor->start().has_value());
    CHECK(f.supervisor->state() == ChildState::Starting);
}

TEST_CASE("a spec without the control channel is refused", "[process][supervisor]") {
    ProcessSpec spec = backend_spec();
    spec.stdio = ports::StdioMode::Capture;
    Fixture f(std::nullopt, true, std::move(spec));
    Result<void> started = f.supervisor->start();
    REQUIRE_FALSE(started.has_value());
    CHECK(started.error().id == "process.spec_needs_control_channel");
    CHECK(f.launcher.children().empty());
}

TEST_CASE("no Hello within the timeout kills the child as HandshakeFailed", "[process][supervisor]") {
    Fixture f;
    REQUIRE(f.supervisor->start().has_value());
    f.rt.advance(2s);
    CHECK(f.child().terminated());
    REQUIRE(f.observer.exits.size() == 1);
    const ChildExitInfo& exit = f.observer.exits[0];
    CHECK(exit.cause == ChildExitCause::HandshakeFailed);
    CHECK(exit.after == AfterExit::Failed);
    REQUIRE(exit.error.has_value());
    CHECK(exit.error->id == "process.child_hello_timeout");
    CHECK(exit.pid == f.child().pid());
    CHECK(exit.status.signal == 9);
    CHECK(f.supervisor->state() == ChildState::Failed);
    CHECK_FALSE(f.supervisor->pid().has_value());
    REQUIRE(f.records.size() == 2);
    CHECK(f.records[1].second == RecordChange::Exited);
}

TEST_CASE("a frame before Hello fails the handshake", "[process][supervisor]") {
    Fixture f;
    REQUIRE(f.supervisor->start().has_value());
    f.child().send(be::Ready{3551});
    f.rt.run_until_idle();
    REQUIRE(f.observer.exits.size() == 1);
    CHECK(f.observer.exits[0].cause == ChildExitCause::HandshakeFailed);
    CHECK(f.observer.exits[0].error->id == "process.child_hello_expected");
    CHECK(f.observer.running.empty());
}

TEST_CASE("a protocol mismatch fails even under a restart policy", "[process][supervisor]") {
    Fixture f(quick_restarts());
    REQUIRE(f.supervisor->start().has_value());
    f.hello(be::kBackendProtocol + 1);
    REQUIRE(f.observer.exits.size() == 1);
    CHECK(f.observer.exits[0].cause == ChildExitCause::HandshakeFailed);
    CHECK(f.observer.exits[0].after == AfterExit::Failed);
    CHECK(f.observer.exits[0].error->id == "process.child_protocol_mismatch");
    f.rt.advance(10s);
    CHECK(f.launcher.children().size() == 1);
    CHECK(f.supervisor->state() == ChildState::Failed);
}

TEST_CASE("events reach the observer; Log, Pong and stderr do not", "[process][supervisor]") {
    Fixture f;
    f.run();
    f.child().send(be::Ready{3551});
    f.child().send(common::Log{LogLevel::Info, 0, "listening"});
    f.child().send(common::Pong{99});
    f.child().write_stderr_line("warning: something");
    f.child().send(be::LoginObserved{"acc", "key"});
    f.rt.run_until_idle();
    CHECK(f.observer.events ==
          std::vector<u64>{contract_frame_type_v<be::Ready>, contract_frame_type_v<be::LoginObserved>});
    CHECK(f.supervisor->state() == ChildState::Running);
}

TEST_CASE("the Hello and the first event in one chunk report running first", "[process][supervisor]") {
    Fixture f;
    REQUIRE(f.supervisor->start().has_value());
    std::vector<u8> bytes = encode_contract_frame(be::BackendHello{be::kBackendProtocol, "1.0", {}});
    const std::vector<u8> ready = encode_contract_frame(be::Ready{3551});
    bytes.insert(bytes.end(), ready.begin(), ready.end());
    f.child().write_stdout(bytes);
    f.rt.run_until_idle();
    CHECK(f.observer.running == std::vector<u32>{1});
    CHECK(f.observer.events == std::vector<u64>{contract_frame_type_v<be::Ready>});
}

TEST_CASE("the child's Ping is answered", "[process][supervisor]") {
    Fixture f;
    f.run();
    f.child().send(common::Ping{41});
    f.rt.run_until_idle();
    CHECK(f.last_sent<common::Pong>().nonce == 41);
    CHECK(f.observer.events.empty());
}

TEST_CASE("Pings go out every interval and a Pong keeps the child alive", "[process][supervisor]") {
    Fixture f(quick_restarts());
    f.run();
    for (int i = 0; i < 6; ++i) {
        f.rt.advance(1s);
        f.child().send(common::Pong{f.last_sent<common::Ping>().nonce});
        f.rt.run_until_idle();
    }
    CHECK(f.child().stdin_frames().count(contract_frame_type_v<common::Ping>) == 6);
    CHECK(f.observer.unresponsive.empty());
    CHECK(f.supervisor->state() == ChildState::Running);
}

TEST_CASE("missed Pongs are only reported without a restart policy", "[process][supervisor]") {
    Fixture f;
    f.run();
    f.rt.advance(4s);
    CHECK(f.observer.unresponsive == std::vector<u32>{3});
    CHECK_FALSE(f.child().terminated());
    CHECK(f.supervisor->state() == ChildState::Running);

    // Pinging goes on, so a late answer clears the count.
    f.child().send(common::Pong{f.last_sent<common::Ping>().nonce});
    f.rt.advance(3s);
    CHECK(f.observer.unresponsive.size() == 1);
}

TEST_CASE("a hang under a restart policy kills and restarts the child", "[process][supervisor]") {
    Fixture f(quick_restarts());
    f.run();
    testing::ScriptedChild& first = f.child();
    f.rt.advance(4s);
    CHECK(f.observer.unresponsive == std::vector<u32>{3});
    CHECK(first.terminated());
    REQUIRE(f.observer.exits.size() == 1);
    const ChildExitInfo& exit = f.observer.exits[0];
    CHECK(exit.cause == ChildExitCause::Unresponsive);
    CHECK(exit.after == AfterExit::Restarting);
    CHECK(exit.restart_delay == 1s);
    CHECK(exit.error->id == "process.child_unresponsive");
    CHECK(f.supervisor->state() == ChildState::Restarting);

    f.rt.advance(1s);
    CHECK(f.launcher.children().size() == 2);
    CHECK(f.supervisor->generation() == 2);
    f.hello();
    CHECK(f.observer.running == std::vector<u32>{1, 2});
}

TEST_CASE("a crash restarts with a doubling delay until the window limit", "[process][supervisor]") {
    Fixture f(quick_restarts());
    f.run();

    f.child().exit({70, std::nullopt});
    f.rt.run_until_idle();
    REQUIRE(f.observer.exits.size() == 1);
    CHECK(f.observer.exits[0].cause == ChildExitCause::Exited);
    CHECK(f.observer.exits[0].error->id == "process.child_exited");
    CHECK(f.observer.exits[0].after == AfterExit::Restarting);
    CHECK(f.observer.exits[0].restart_delay == 1s);

    f.rt.advance(1s);
    REQUIRE(f.launcher.children().size() == 2);
    f.child().exit({70, std::nullopt});
    f.rt.run_until_idle();
    REQUIRE(f.observer.exits.size() == 2);
    CHECK(f.observer.exits[1].generation == 2);
    CHECK(f.observer.exits[1].cause == ChildExitCause::Exited);
    CHECK(f.observer.exits[1].restart_delay == 2s);

    f.rt.advance(2s);
    REQUIRE(f.launcher.children().size() == 3);
    f.child().exit(kSigKill);
    f.rt.run_until_idle();
    REQUIRE(f.observer.exits.size() == 3);
    const ChildExitInfo& last = f.observer.exits[2];
    CHECK(last.after == AfterExit::Failed);
    REQUIRE(last.error.has_value());
    CHECK(last.error->id == "process.child_restart_limit");
    REQUIRE(last.error->causes.size() == 1);
    CHECK(last.error->causes[0].id == "process.child_signaled");
    CHECK(f.supervisor->state() == ChildState::Failed);
}

TEST_CASE("restarts outside the window no longer count", "[process][supervisor]") {
    Fixture f(quick_restarts());
    f.run();
    for (const auto delay : {1s, 2s}) {
        f.child().exit({1, std::nullopt});
        f.rt.advance(delay);
        f.hello();
    }
    REQUIRE(f.launcher.children().size() == 3);
    for (int i = 0; i < 120; ++i) {
        f.rt.advance(1s);
        f.child().send(common::Pong{f.last_sent<common::Ping>().nonce});
    }
    REQUIRE(f.supervisor->state() == ChildState::Running);
    f.child().exit({1, std::nullopt});
    f.rt.run_until_idle();
    REQUIRE(f.observer.exits.size() == 3);
    CHECK(f.observer.exits[2].after == AfterExit::Restarting);
    CHECK(f.observer.exits[2].restart_delay == 1s);
}

TEST_CASE("a failed respawn ends in SpawnFailed and is retried under the policy", "[process][supervisor]") {
    Fixture f(quick_restarts());
    f.run();
    f.fail_spawns = 1;
    f.child().exit({1, std::nullopt});
    f.rt.advance(1s);
    REQUIRE(f.observer.exits.size() == 2);
    const ChildExitInfo& failed = f.observer.exits[1];
    CHECK(failed.cause == ChildExitCause::SpawnFailed);
    CHECK_FALSE(failed.pid.has_value());
    CHECK(failed.error->id == "process.test_spawn_failed");
    CHECK(failed.after == AfterExit::Restarting);
    CHECK(failed.restart_delay == 2s);

    f.rt.advance(2s);
    CHECK(f.launcher.children().size() == 2);
    CHECK(f.supervisor->state() == ChildState::Starting);
}

TEST_CASE("an exit without a restart policy is a failure", "[process][supervisor]") {
    Fixture f;
    f.run();
    f.child().exit({0, std::nullopt});
    f.rt.run_until_idle();
    REQUIRE(f.observer.exits.size() == 1);
    CHECK(f.observer.exits[0].cause == ChildExitCause::Exited);
    CHECK(f.observer.exits[0].after == AfterExit::Failed);
    CHECK(f.observer.exits[0].status.code == 0);
    CHECK(f.supervisor->state() == ChildState::Failed);
}

TEST_CASE("malformed output after the handshake is a protocol error", "[process][supervisor]") {
    Fixture f(quick_restarts());
    f.run();
    sb::wire::Writer writer;
    writer.quic_varint(contract_frame_type_v<be::Ready>);
    writer.quic_varint(kChildFrameCap + 1);
    f.child().write_stdout(writer.take());
    f.rt.run_until_idle();
    CHECK(f.child().terminated());
    REQUIRE(f.observer.exits.size() == 1);
    CHECK(f.observer.exits[0].cause == ChildExitCause::ProtocolError);
    CHECK(f.observer.exits[0].error->id == "process.child_frame_too_large");
    CHECK(f.observer.exits[0].after == AfterExit::Restarting);
}

TEST_CASE("stop closes stdin and a child that exits reports Requested", "[process][supervisor]") {
    Fixture f(quick_restarts());
    f.run();
    CHECK(f.supervisor->stop(5s));
    CHECK(f.supervisor->state() == ChildState::Stopping);
    CHECK(f.child().stdin_closed());
    f.child().exit({0, std::nullopt});
    f.rt.advance(10s);
    REQUIRE(f.observer.exits.size() == 1);
    CHECK(f.observer.exits[0].cause == ChildExitCause::Requested);
    CHECK(f.observer.exits[0].after == AfterExit::Stopped);
    CHECK_FALSE(f.observer.exits[0].error.has_value());
    CHECK_FALSE(f.child().terminated());
    CHECK(f.supervisor->state() == ChildState::Stopped);
    CHECK(f.launcher.children().size() == 1);
}

TEST_CASE("stop kills a child that outlives the grace", "[process][supervisor]") {
    Fixture f;
    f.run();
    CHECK(f.supervisor->stop(5s));
    f.rt.advance(4900ms);
    CHECK_FALSE(f.child().terminated());
    f.rt.advance(200ms);
    CHECK(f.child().terminated());
    REQUIRE(f.observer.exits.size() == 1);
    CHECK(f.observer.exits[0].cause == ChildExitCause::Requested);
    CHECK(f.observer.exits[0].after == AfterExit::Stopped);
    CHECK(f.supervisor->state() == ChildState::Stopped);
}

TEST_CASE("stop during the handshake reports Requested, not a hello timeout", "[process][supervisor]") {
    Fixture f;
    REQUIRE(f.supervisor->start().has_value());
    CHECK(f.supervisor->stop(5s));
    f.rt.advance(10s);
    REQUIRE(f.observer.exits.size() == 1);
    CHECK(f.observer.exits[0].cause == ChildExitCause::Requested);
}

TEST_CASE("stop with no live child is Stopped at once without on_exit", "[process][supervisor]") {
    Fixture f(quick_restarts());
    CHECK_FALSE(f.supervisor->stop());
    CHECK(f.supervisor->state() == ChildState::Stopped);

    f.run();
    f.child().exit({1, std::nullopt});
    f.rt.run_until_idle();
    REQUIRE(f.supervisor->state() == ChildState::Restarting);
    CHECK_FALSE(f.supervisor->stop());
    CHECK(f.supervisor->state() == ChildState::Stopped);
    f.rt.advance(10s);
    CHECK(f.launcher.children().size() == 1);
    CHECK(f.observer.exits.size() == 1);
}

TEST_CASE("a typed request completes with the matching reply", "[process][supervisor]") {
    Fixture f;
    f.run();
    std::optional<Result<be::HealthReply>> got;
    f.supervisor->request<be::HealthReply>(be::Health{}, [&](Result<be::HealthReply> reply) { got = std::move(reply); });
    const be::Health sent = f.last_sent<be::Health>();
    CHECK(sent.req_id != 0);

    // Another req_id is ignored.
    f.child().send(be::HealthReply{sent.req_id + 100, "other", {}, 1});
    f.rt.run_until_idle();
    CHECK_FALSE(got.has_value());

    f.child().send(be::HealthReply{sent.req_id, "1.2.3", {}, 3551});
    f.rt.run_until_idle();
    REQUIRE(got.has_value());
    REQUIRE(got->has_value());
    CHECK((*got)->version == "1.2.3");
    CHECK((*got)->http_port == 3551);
    CHECK(f.observer.events.empty());
}

TEST_CASE("concurrent requests get distinct ids and complete independently", "[process][supervisor]") {
    Fixture f;
    f.run();
    std::vector<int> order;
    f.supervisor->command(be::Drain{}, [&](Result<void> r) {
        REQUIRE(r.has_value());
        order.push_back(1);
    });
    const u64 first = f.last_sent<be::Drain>().req_id;
    f.supervisor->command(be::Drain{}, [&](Result<void> r) {
        REQUIRE(r.has_value());
        order.push_back(2);
    });
    const u64 second = f.last_sent<be::Drain>().req_id;
    CHECK(first != second);

    common::CommandResult ok;
    ok.ok = true;
    ok.req_id = second;
    f.child().send(ok);
    ok.req_id = first;
    f.child().send(ok);
    f.rt.run_until_idle();
    CHECK(order == std::vector<int>{2, 1});
}

TEST_CASE("a CommandResult error, Unsupported or a wrong reply fails the request", "[process][supervisor]") {
    Fixture f;
    f.run();

    std::optional<Result<be::HealthReply>> health;
    f.supervisor->request<be::HealthReply>(be::Health{}, [&](Result<be::HealthReply> r) { health = std::move(r); });
    common::CommandResult failed;
    failed.req_id = f.last_sent<be::Health>().req_id;
    failed.error = common::to_wire(make_diag(ErrorDomain::Process, MessageId{"process.test_busy"}).build());
    f.child().send(failed);

    std::optional<Result<void>> drain;
    f.supervisor->command(be::Drain{}, [&](Result<void> r) { drain = std::move(r); });
    f.child().send(common::Unsupported{f.last_sent<be::Drain>().req_id});

    std::optional<Result<be::ContentInfoReply>> content;
    f.supervisor->request<be::ContentInfoReply>(be::ContentInfo{},
                                                [&](Result<be::ContentInfoReply> r) { content = std::move(r); });
    common::CommandResult bare_ok;
    bare_ok.ok = true;
    bare_ok.req_id = f.last_sent<be::ContentInfo>().req_id;
    f.child().send(bare_ok);

    std::optional<Result<void>> anonymous;
    f.supervisor->command(be::PurgeData{}, [&](Result<void> r) { anonymous = std::move(r); });
    common::CommandResult no_error;
    no_error.req_id = f.last_sent<be::PurgeData>().req_id;
    f.child().send(no_error);

    f.rt.run_until_idle();
    REQUIRE(health.has_value());
    REQUIRE_FALSE(health->has_value());
    CHECK(health->error().id == "process.test_busy");
    REQUIRE(drain.has_value());
    REQUIRE_FALSE(drain->has_value());
    CHECK(drain->error().id == "process.child_request_unsupported");
    CHECK(drain->error().kind == ErrorKind::Unsupported);
    REQUIRE(content.has_value());
    REQUIRE_FALSE(content->has_value());
    CHECK(content->error().id == "process.child_unexpected_reply");
    REQUIRE(anonymous.has_value());
    REQUIRE_FALSE(anonymous->has_value());
    CHECK(anonymous->error().id == "process.child_request_failed");
    CHECK(f.supervisor->state() == ChildState::Running);
}

TEST_CASE("a request before Running fails later with child_not_running", "[process][supervisor]") {
    Fixture f;
    std::optional<Result<void>> done;
    f.supervisor->command(be::Drain{}, [&](Result<void> r) { done = std::move(r); });
    CHECK_FALSE(done.has_value());
    f.rt.run_until_idle();
    REQUIRE(done.has_value());
    REQUIRE_FALSE(done->has_value());
    CHECK(done->error().id == "process.child_not_running");

    REQUIRE(f.supervisor->start().has_value());
    done.reset();
    f.supervisor->command(be::Drain{}, [&](Result<void> r) { done = std::move(r); });
    f.rt.run_until_idle();
    REQUIRE(done.has_value());
    CHECK(done->error().id == "process.child_not_running");
    CHECK(f.child().stdin_frames().count(contract_frame_type_v<be::Drain>) == 0);
}

TEST_CASE("pending requests fail with child_gone before on_exit, and a late reply is ignored", "[process][supervisor]") {
    Fixture f(quick_restarts());
    f.run();
    std::vector<std::string> order;
    f.observer.exits.clear();
    f.supervisor->command(be::Drain{}, [&](Result<void> r) {
        REQUIRE_FALSE(r.has_value());
        CHECK(r.error().id == "process.child_gone");
        CHECK(f.observer.exits.empty());
        order.emplace_back("done");
    });
    const u64 req_id = f.last_sent<be::Drain>().req_id;
    f.child().exit({1, std::nullopt});
    f.rt.run_until_idle();
    CHECK(order == std::vector<std::string>{"done"});
    REQUIRE(f.observer.exits.size() == 1);

    f.rt.advance(1s);
    f.hello();
    common::CommandResult late;
    late.ok = true;
    late.req_id = req_id;
    f.child().send(late);
    f.rt.run_until_idle();
    CHECK(order.size() == 1);
    CHECK(f.supervisor->state() == ChildState::Running);
}

TEST_CASE("stop from a child_gone callback cancels the restart", "[process][supervisor]") {
    Fixture f(quick_restarts());
    f.run();
    std::optional<bool> stopped;
    f.supervisor->command(be::Drain{}, [&](Result<void> r) {
        REQUIRE_FALSE(r.has_value());
        CHECK(r.error().id == "process.child_gone");
        stopped = f.supervisor->stop();
    });
    f.child().exit({1, std::nullopt});
    f.rt.run_until_idle();
    REQUIRE(stopped.has_value());
    CHECK_FALSE(*stopped);
    CHECK(f.supervisor->state() == ChildState::Stopped);
    REQUIRE(f.observer.exits.size() == 1);
    CHECK(f.observer.exits[0].cause == ChildExitCause::Exited);
    CHECK(f.observer.exits[0].after == AfterExit::Stopped);
    f.rt.advance(10s);
    CHECK(f.launcher.children().size() == 1);
    CHECK(f.supervisor->state() == ChildState::Stopped);
}

TEST_CASE("a child request is answered through its ChildReply", "[process][supervisor][reply]") {
    Fixture f;
    f.run();
    f.child().send(be::ResolveMatchTarget{17, "acc", "solo"});
    f.child().send(be::ResolveMatchTarget{18, "acc2", "duo"});
    f.child().send(be::ResolveMatchTarget{19, "acc3", "duo"});
    f.child().send(be::ResolveMatchTarget{20, "acc4", "duo"});
    f.rt.run_until_idle();
    REQUIRE(f.handler.replies.size() == 4);
    CHECK(f.handler.accounts == std::vector<std::string>{"acc", "acc2", "acc3", "acc4"});
    CHECK(f.handler.replies[0].req_id() == 17);
    CHECK(f.observer.events.empty());

    be::MatchTarget target;
    target.endpoint = "1.2.3.4";
    f.handler.replies[0].reply(target);
    f.handler.replies[1].ok();
    f.handler.replies[2].fail(make_diag(ErrorDomain::Process, MessageId{"process.test_failure"}).build());
    f.handler.replies[3].unsupported();
    // A second answer is a no-op.
    f.handler.replies[1].ok();

    const be::MatchTarget sent = f.last_sent<be::MatchTarget>();
    CHECK(sent.req_id == 17);
    CHECK(sent.endpoint == "1.2.3.4");
    Result<std::vector<common::CommandResult>> results = f.child().stdin_frames().all<common::CommandResult>();
    REQUIRE(results.has_value());
    REQUIRE(results->size() == 2);
    CHECK((*results)[0].req_id == 18);
    CHECK((*results)[0].ok);
    CHECK((*results)[1].req_id == 19);
    CHECK_FALSE((*results)[1].ok);
    REQUIRE((*results)[1].error.has_value());
    CHECK((*results)[1].error->id == "process.test_failure");
    CHECK(f.last_sent<common::Unsupported>().req_id == 20);
}

TEST_CASE("a ChildReply dropped unanswered sends internal.bug, once", "[process][supervisor][reply]") {
    Fixture f;
    f.run();
    f.child().send(be::ResolveMatchTarget{5, "acc", "solo"});
    f.rt.run_until_idle();
    REQUIRE(f.handler.replies.size() == 1);
    {
        ChildReply moved = std::move(f.handler.replies[0]);
        CHECK(moved.req_id() == 5);
        f.handler.replies.clear();
        CHECK(f.child().stdin_frames().count(contract_frame_type_v<common::CommandResult>) == 0);
    }
    Result<std::vector<common::CommandResult>> results = f.child().stdin_frames().all<common::CommandResult>();
    REQUIRE(results.has_value());
    REQUIRE(results->size() == 1);
    CHECK((*results)[0].req_id == 5);
    CHECK_FALSE((*results)[0].ok);
    REQUIRE((*results)[0].error.has_value());
    CHECK((*results)[0].error->id == "internal.bug");
}

TEST_CASE("move-assigning over an unanswered ChildReply answers the old one", "[process][supervisor][reply]") {
    Fixture f;
    f.run();
    f.child().send(be::ResolveMatchTarget{1, "a", "solo"});
    f.child().send(be::ResolveMatchTarget{2, "b", "solo"});
    f.rt.run_until_idle();
    REQUIRE(f.handler.replies.size() == 2);
    f.handler.replies[0] = std::move(f.handler.replies[1]);
    CHECK(f.handler.replies[0].req_id() == 2);
    f.handler.replies[0].ok();
    f.handler.replies.clear();

    Result<std::vector<common::CommandResult>> results = f.child().stdin_frames().all<common::CommandResult>();
    REQUIRE(results.has_value());
    REQUIRE(results->size() == 2);
    CHECK((*results)[0].req_id == 1);
    CHECK_FALSE((*results)[0].ok);
    CHECK((*results)[1].req_id == 2);
    CHECK((*results)[1].ok);
}

TEST_CASE("a reply after the child was respawned is dropped", "[process][supervisor][reply]") {
    Fixture f(quick_restarts());
    f.run();
    f.child().send(be::ResolveMatchTarget{9, "acc", "solo"});
    f.rt.run_until_idle();
    REQUIRE(f.handler.replies.size() == 1);
    f.child().exit({1, std::nullopt});
    f.rt.advance(1s);
    f.hello();
    REQUIRE(f.supervisor->generation() == 2);

    f.handler.replies[0].reply(be::MatchTarget{});
    f.handler.replies.clear();
    CHECK(f.child().stdin_frames().count(contract_frame_type_v<be::MatchTarget>) == 0);
    CHECK(f.child().stdin_frames().count(contract_frame_type_v<common::CommandResult>) == 0);
}

TEST_CASE("without a request handler a child request is an event", "[process][supervisor]") {
    Fixture f(std::nullopt, false);
    f.run();
    f.child().send(be::ResolveMatchTarget{3, "acc", "solo"});
    f.rt.run_until_idle();
    CHECK(f.observer.events == std::vector<u64>{contract_frame_type_v<be::ResolveMatchTarget>});
    CHECK(f.child().stdin_frames().count(contract_frame_type_v<common::CommandResult>) == 0);
}

TEST_CASE("destruction kills the live child without on_exit and detaches replies", "[process][supervisor]") {
    Fixture f;
    f.run();
    f.child().send(be::ResolveMatchTarget{4, "acc", "solo"});
    f.rt.run_until_idle();
    REQUIRE(f.handler.replies.size() == 1);
    bool pending_called = false;
    f.supervisor->command(be::Drain{}, [&](Result<void>) { pending_called = true; });
    testing::ScriptedChild& child = f.child();
    const std::size_t written = child.stdin_frames().frames().size();

    f.supervisor.reset();
    CHECK(child.terminated());
    REQUIRE(f.records.size() == 2);
    CHECK(f.records[1].second == RecordChange::Exited);

    f.handler.replies[0].ok();
    f.handler.replies.clear();
    f.rt.advance(10s);
    CHECK_FALSE(pending_called);
    CHECK(f.observer.exits.empty());
    CHECK(child.stdin_frames().frames().size() == written);
}

TEST_CASE("destruction drops a not-running answer still queued", "[process][supervisor]") {
    Fixture f;
    bool called = false;
    f.supervisor->command(be::Drain{}, [&](Result<void>) { called = true; });
    f.supervisor.reset();
    f.rt.run_until_idle();
    CHECK_FALSE(called);
}

TEST_CASE("start after Failed resets the restart window", "[process][supervisor]") {
    Fixture f(quick_restarts());
    f.run();
    for (const auto delay : {1s, 2s}) {
        f.child().exit({1, std::nullopt});
        f.rt.advance(delay);
        f.hello();
    }
    f.child().exit({1, std::nullopt});
    f.rt.run_until_idle();
    REQUIRE(f.supervisor->state() == ChildState::Failed);
    REQUIRE(f.supervisor->start().has_value());
    f.hello();
    f.child().exit({1, std::nullopt});
    f.rt.run_until_idle();
    CHECK(f.observer.exits.back().after == AfterExit::Restarting);
    CHECK(f.observer.exits.back().restart_delay == 1s);
}

TEST_CASE("a conforming FakeBackend runs, answers and stops on stdin EOF", "[process][supervisor][fake]") {
    Fixture f(quick_restarts());
    f.fake = testing::FakeBackendScript{};
    REQUIRE(f.supervisor->start().has_value());
    f.rt.run_until_idle();
    CHECK(f.observer.running == std::vector<u32>{1});
    CHECK(f.observer.events == std::vector<u64>{contract_frame_type_v<be::Ready>});

    std::optional<Result<be::HealthReply>> health;
    f.supervisor->request<be::HealthReply>(be::Health{}, [&](Result<be::HealthReply> r) { health = std::move(r); });
    f.rt.advance(3s);
    REQUIRE(health.has_value());
    REQUIRE(health->has_value());
    CHECK((*health)->version == "fake");
    CHECK(f.observer.unresponsive.empty());

    CHECK(f.supervisor->stop(5s));
    f.rt.run_until_idle();
    REQUIRE(f.observer.exits.size() == 1);
    CHECK(f.observer.exits[0].cause == ChildExitCause::Requested);
    CHECK_FALSE(f.child().terminated());
    CHECK(f.supervisor->state() == ChildState::Stopped);
}
