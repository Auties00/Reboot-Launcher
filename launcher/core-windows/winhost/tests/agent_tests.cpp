#include "loopback_engine.hpp"  // first: pulls in win32.hpp before any std header

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <future>
#include <memory>
#include <optional>
#include <string>
#include <thread>

#include "reboot/contracts/common.hpp"
#include "reboot/contracts/winhost.hpp"
#include "reboot/foundation/version.hpp"
#include "reboot/os_windows/winhost/control_connection.hpp"
#include "reboot/os_windows/winhost/winhost_agent.hpp"

using namespace rb;
using namespace rb::os_windows::winhost;
using rb::os_windows::winhost::test::LoopbackEngine;
using rb::os_windows::winhost::test::system_path;
using rb::os_windows::winhost::test::utf16;
namespace wh = rb::contracts::winhost;
namespace common = rb::contracts::common;

using namespace std::chrono_literals;

namespace {

constexpr i64 kFileNotFound = 2;
constexpr i64 kPathNotFound = 3;

// One winhost agent serving a loopback engine on its own thread. The engine's end is closed
// before the thread is joined, so a failed check never leaves run() blocked.
class Harness {
public:
    Harness() {
        auto connected = ControlConnection::connect(engine.port());
        REQUIRE(connected);
        connection = std::move(*connected);
        engine.accept_peer();
        const auto preamble = engine.read_preamble();
        REQUIRE(preamble);
        CHECK(*preamble == game_control_preamble(VersionStreams::payload_abi));
        agent = std::make_unique<WinhostAgent>(*connection);
    }

    ~Harness() {
        engine.close_peer();
        if (runner.joinable()) runner.join();
    }

    void start() {
        auto done = std::make_shared<std::promise<WinhostExit>>();
        exit = done->get_future();
        runner = std::thread([this, done] { done->set_value(agent->run()); });
    }

    [[nodiscard]] std::optional<WinhostExit> exit_within(std::chrono::milliseconds bound) {
        if (exit.wait_for(bound) != std::future_status::ready) return std::nullopt;
        return exit.get();
    }

    LoopbackEngine engine;
    std::unique_ptr<ControlConnection> connection;
    std::unique_ptr<WinhostAgent> agent;

private:
    std::future<WinhostExit> exit;
    std::thread runner;
};

wh::SpawnGame spawn_of(std::wstring_view exe, std::initializer_list<std::wstring_view> args) {
    wh::SpawnGame spawn;
    spawn.exe_utf16 = utf16(exe);
    for (const auto arg : args) spawn.argv_utf16.push_back(utf16(arg));
    spawn.cwd_utf16 = utf16(system_path(L""));
    spawn.inject_timeout_ms = 5000;
    spawn.drain_timeout_ms = 5000;
    return spawn;
}

// A game that keeps running for a minute unless it is killed.
wh::SpawnGame long_running_game() { return spawn_of(system_path(L"PING.EXE"), {L"-n", L"60", L"127.0.0.1"}); }

struct OwnedProcess {
    HANDLE handle = nullptr;
    explicit OwnedProcess(u32 pid) : handle(OpenProcess(SYNCHRONIZE, FALSE, pid)) {}
    ~OwnedProcess() {
        if (handle != nullptr) CloseHandle(handle);
    }
    OwnedProcess(const OwnedProcess&) = delete;
    OwnedProcess& operator=(const OwnedProcess&) = delete;

    [[nodiscard]] bool ended_within(DWORD ms) const { return WaitForSingleObject(handle, ms) == WAIT_OBJECT_0; }
};

}  // namespace

TEST_CASE("connect writes the preamble and hello sends the token, protocol, build and pid") {
    Harness harness;
    std::array<u8, 32> raw{};
    for (std::size_t i = 0; i < raw.size(); ++i) raw[i] = static_cast<u8>(i * 7);
    REQUIRE(harness.agent->hello(ControlTokenBytes{raw}));

    const auto hello = harness.engine.expect<wh::WhHello>();
    REQUIRE(hello);
    CHECK(hello->token == raw);
    CHECK(hello->protocol == wh::kWinhostProtocol);
    CHECK(hello->pid == GetCurrentProcessId());
    CHECK_FALSE(hello->build.empty());
}

TEST_CASE("connect to a port nobody listens on fails at the connect step") {
    u16 port = 0;
    {
        LoopbackEngine closed;
        port = closed.port();
    }
    const auto connected = ControlConnection::connect(port);
    REQUIRE_FALSE(connected);
    CHECK(connected.error().step == FailureStep::Connect);
    CHECK(connected.error().os_code.has_value());
}

TEST_CASE("Ping gets Pong, an unknown request gets Unsupported, and EOF before Welcome is Refused") {
    Harness harness;
    harness.start();
    harness.engine.send(common::Ping{77});
    const auto pong = harness.engine.expect<common::Pong>();
    REQUIRE(pong);
    CHECK(pong->nonce == 77);

    // Type 0x5FF with field 1 = 42, the req_id of a request this winhost does not know.
    const std::array<u8, 5> unknown{0x45, 0xFF, 0x02, 0x08, 42};
    harness.engine.send_raw(unknown);
    const auto unsupported = harness.engine.expect<common::Unsupported>();
    REQUIRE(unsupported);
    CHECK(unsupported->req_id == 42);

    harness.engine.close_peer();
    CHECK(harness.exit_within(10s) == WinhostExit::Refused);
}

TEST_CASE("a request before Welcome is a protocol error") {
    Harness harness;
    harness.start();
    harness.engine.send(wh::Resume{1});
    const auto fatal = harness.engine.expect<wh::WhFatal>();
    REQUIRE(fatal);
    CHECK(fatal->step == "protocol");
    CHECK(harness.engine.drain_to_eof());
    CHECK(harness.exit_within(10s) == WinhostExit::ProtocolError);
}

TEST_CASE("a frame over the game-control cap is a protocol error") {
    Harness harness;
    harness.start();
    const u64 length = kGameControlFrameCap + 1;
    const std::array<u8, 5> header{0x10, static_cast<u8>(0x80 | (length >> 24)), static_cast<u8>(length >> 16),
                                   static_cast<u8>(length >> 8), static_cast<u8>(length)};
    harness.engine.send_raw(header);
    CHECK(harness.engine.drain_to_eof());
    CHECK(harness.exit_within(10s) == WinhostExit::ProtocolError);
}

TEST_CASE("next_frame returns EOF when the engine closes between frames") {
    LoopbackEngine engine;
    auto connection = ControlConnection::connect(engine.port());
    REQUIRE(connection);
    engine.accept_peer();
    REQUIRE(engine.read_preamble());
    engine.send(common::Ping{1});
    engine.close_peer();

    const auto first = (*connection)->next_frame();
    REQUIRE(first);
    REQUIRE(first->has_value());
    CHECK((*first)->type == contract_frame_type_v<common::Ping>);
    const auto end = (*connection)->next_frame();
    REQUIRE(end);
    CHECK_FALSE(end->has_value());
}

TEST_CASE("next_frame reports a frame cut off midway as a protocol error") {
    LoopbackEngine engine;
    auto connection = ControlConnection::connect(engine.port());
    REQUIRE(connection);
    engine.accept_peer();
    REQUIRE(engine.read_preamble());
    const std::array<u8, 3> partial{0x10, 0x05, 0x08};  // promises 5 payload bytes, sends 1
    engine.send_raw(partial);
    engine.close_peer();

    const auto frame = (*connection)->next_frame();
    REQUIRE_FALSE(frame);
    CHECK(frame.error().step == FailureStep::Protocol);
}

TEST_CASE("shutdown wakes a reader blocked in next_frame although the engine keeps its end open") {
    LoopbackEngine engine;
    auto connection = ControlConnection::connect(engine.port());
    REQUIRE(connection);
    engine.accept_peer();
    ControlConnection& conn = **connection;

    auto reader = std::async(std::launch::async, [&conn] { return conn.next_frame(); });
    // Give the reader time to block in recv; the wake must work whether or not it got there.
    CHECK(reader.wait_for(200ms) == std::future_status::timeout);
    conn.shutdown();
    REQUIRE(reader.wait_for(10s) == std::future_status::ready);
    const auto result = reader.get();
    REQUIRE(result);
    CHECK_FALSE(result->has_value());
    CHECK(engine.drain_to_eof());
}

TEST_CASE("EOF after Welcome terminates the Job and returns EngineClosed") {
    Harness harness;
    harness.start();
    harness.engine.send(wh::WhWelcome{long_running_game()});
    const auto spawned = harness.engine.expect<wh::Spawned>();
    REQUIRE(spawned);
    CHECK(spawned->role == wh::ProcessRole::Game);
    const OwnedProcess game(spawned->pid);
    REQUIRE(game.handle != nullptr);

    harness.engine.send(wh::Resume{1});
    const auto resumed = harness.engine.expect<common::CommandResult>();
    REQUIRE(resumed);
    CHECK(resumed->req_id == 1);
    CHECK(resumed->ok);
    CHECK_FALSE(game.ended_within(0));

    // The engine dies: winhost must take the game down with it.
    harness.engine.close_peer();
    CHECK(harness.exit_within(15s) == WinhostExit::EngineClosed);
    CHECK(game.ended_within(5000));
}

TEST_CASE("Stop is answered at once, the game's Exited follows and winhost disconnects with Stopped") {
    Harness harness;
    harness.start();
    harness.engine.send(wh::WhWelcome{long_running_game()});
    const auto spawned = harness.engine.expect<wh::Spawned>();
    REQUIRE(spawned);
    const OwnedProcess game(spawned->pid);
    harness.engine.send(wh::Resume{1});
    REQUIRE(harness.engine.expect<common::CommandResult>());

    harness.engine.send(wh::Stop{2, 0});
    const auto stopped = harness.engine.expect<common::CommandResult>();
    REQUIRE(stopped);
    CHECK(stopped->req_id == 2);
    CHECK(stopped->ok);
    const auto exited = harness.engine.expect<wh::Exited>();
    REQUIRE(exited);
    CHECK(exited->role == wh::ProcessRole::Game);
    CHECK(harness.engine.drain_to_eof());
    CHECK(harness.exit_within(15s) == WinhostExit::Stopped);
    CHECK(game.ended_within(0));
}

TEST_CASE("the game gets the engine's variables laid over winhost's own environment") {
    Harness harness;
    harness.start();
    auto spawn = spawn_of(system_path(L"cmd.exe"), {L"/d", L"/c", L"echo", L"%REBOOT_WINHOST_TEST%-%SystemRoot%"});
    spawn.env_block_utf16 = utf16(std::wstring(L"REBOOT_WINHOST_TEST=layered") + L'\0' + L'\0');
    harness.engine.send(wh::WhWelcome{std::move(spawn)});
    REQUIRE(harness.engine.expect<wh::Spawned>());
    harness.engine.send(wh::Resume{1});
    REQUIRE(harness.engine.expect<wh::Exited>());
    // Stop drains the output pipes before winhost disconnects.
    harness.engine.send(wh::Stop{2, 0});
    CHECK(harness.engine.drain_to_eof());
    CHECK(harness.exit_within(15s) == WinhostExit::Stopped);

    std::string output;
    for (const auto& chunk : harness.engine.skipped_of<wh::Output>())
        if (chunk.role == wh::ProcessRole::Game && chunk.stream == wh::OutputStream::Stdout)
            output.append(chunk.bytes.begin(), chunk.bytes.end());
    std::array<char, MAX_PATH> root{};
    const DWORD root_length = GetEnvironmentVariableA("SystemRoot", root.data(), static_cast<DWORD>(root.size()));
    REQUIRE(root_length > 0);
    CHECK(output.find("layered-" + std::string(root.data(), root_length)) != std::string::npos);
}

TEST_CASE("a game that cannot be created sends WhFatal at the spawn step and ends with LaunchFailed") {
    Harness harness;
    harness.start();
    harness.engine.send(wh::WhWelcome{spawn_of(L"C:\\reboot-missing\\missing-game.exe", {})});
    const auto fatal = harness.engine.expect<wh::WhFatal>();
    REQUIRE(fatal);
    CHECK(fatal->step == "spawn");
    REQUIRE(fatal->os_code);
    CHECK((*fatal->os_code == kFileNotFound || *fatal->os_code == kPathNotFound));
    CHECK(harness.engine.drain_to_eof());
    CHECK(harness.exit_within(15s) == WinhostExit::LaunchFailed);
}

TEST_CASE("an Early DLL whose sha256 does not match is not loaded and fails the launch") {
    Harness harness;
    harness.start();
    auto spawn = long_running_game();
    spawn.inject.push_back(
        wh::InjectSpec{utf16(system_path(L"version.dll")), {}, wh::BootStrategy::EarlyBirdApc, wh::InjectPhase::Early});
    harness.engine.send(wh::WhWelcome{std::move(spawn)});

    const auto injected = harness.engine.expect<wh::Injected>();
    REQUIRE(injected);
    CHECK_FALSE(injected->ok);
    CHECK(injected->error == kInjectHashMismatch);
    const auto fatal = harness.engine.expect<wh::WhFatal>();
    REQUIRE(fatal);
    CHECK(fatal->step == "inject");
    CHECK(fatal->os_code == kInjectHashMismatch);
    CHECK(harness.engine.drain_to_eof());
    CHECK(harness.exit_within(15s) == WinhostExit::LaunchFailed);

    const auto spawned = harness.engine.skipped_of<wh::Spawned>();
    REQUIRE(spawned.size() == 1);
    const OwnedProcess game(spawned.front().pid);
    CHECK((game.handle == nullptr || game.ended_within(0)));
}

TEST_CASE("an Inject request with a mismatched sha256 reports Injected and a failed CommandResult") {
    Harness harness;
    harness.start();
    harness.engine.send(wh::WhWelcome{long_running_game()});
    REQUIRE(harness.engine.expect<wh::Spawned>());
    harness.engine.send(wh::Resume{1});
    REQUIRE(harness.engine.expect<common::CommandResult>());

    const auto path = utf16(system_path(L"version.dll"));
    harness.engine.send(wh::Inject{3, wh::InjectSpec{path, {}, wh::BootStrategy::AfterResume, wh::InjectPhase::LoggedIn}});
    const auto injected = harness.engine.expect<wh::Injected>();
    REQUIRE(injected);
    CHECK(injected->path_utf16 == path);
    CHECK_FALSE(injected->ok);
    CHECK(injected->error == kInjectHashMismatch);
    const auto reply = harness.engine.expect<common::CommandResult>();
    REQUIRE(reply);
    CHECK(reply->req_id == 3);
    CHECK_FALSE(reply->ok);
    REQUIRE(reply->error);
    CHECK(reply->error->id == "game_channel.request_failed");
    CHECK(reply->error->os_origin == SystemError::Origin::GuestWindows);
    CHECK(reply->error->os_code == kInjectHashMismatch);

    harness.engine.close_peer();
    CHECK(harness.exit_within(15s) == WinhostExit::EngineClosed);
}

TEST_CASE("a second Welcome is a protocol error that also ends the running game") {
    Harness harness;
    harness.start();
    harness.engine.send(wh::WhWelcome{long_running_game()});
    const auto spawned = harness.engine.expect<wh::Spawned>();
    REQUIRE(spawned);
    const OwnedProcess game(spawned->pid);
    harness.engine.send(wh::WhWelcome{long_running_game()});
    const auto fatal = harness.engine.expect<wh::WhFatal>();
    REQUIRE(fatal);
    CHECK(fatal->step == "protocol");
    CHECK(harness.engine.drain_to_eof());
    CHECK(harness.exit_within(15s) == WinhostExit::ProtocolError);
    CHECK(game.ended_within(5000));
}
