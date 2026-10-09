#include <array>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#include <boost/asio/executor_work_guard.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/read.hpp>
#include <boost/asio/write.hpp>

#include <catch2/catch_test_macros.hpp>

#include "reboot/contracts/common.hpp"
#include "reboot/contracts/game_client.hpp"
#include "reboot/contracts/winhost.hpp"
#include "reboot/foundation/framing.hpp"
#include "reboot/foundation/sha256.hpp"
#include "reboot/foundation/text.hpp"
#include "reboot/testing/deterministic_runtime.hpp"
#include "reboot/testing/fake_client_dll.hpp"
#include "reboot/testing/fake_platform_paths.hpp"
#include "reboot/testing/fake_session_host.hpp"
#include "reboot/testing/fake_winhost.hpp"
#include "reboot/testing/frame_log.hpp"
#include "reboot/testing/game_control_bootstrap.hpp"
#include "reboot/testing/manual_waiter.hpp"
#include "reboot/testing/memory_stream_pair.hpp"
#include "reboot/testing/port_conformance.hpp"
#include "reboot/testing/scripted_process_launcher.hpp"
#include "tcp_stream.hpp"

using namespace rb;
using namespace rb::testing;
using namespace std::chrono_literals;
namespace gc = contracts::game_client;
namespace wh = contracts::winhost;
namespace common = contracts::common;

namespace {

constexpr std::string_view kToken = "00112233445566778899aabbccddeeff00112233445566778899aabbccddeeff";
constexpr std::string_view kSession = "12345678-9abc-4def-8123-456789abcdef";

[[nodiscard]] ports::EnvBlock bootstrap_env(u16 port = 41000) {
    return {{{"PATH", "C:\\Windows"},
             {"REBOOT_CTL", "tcp://127.0.0.1:" + std::to_string(port)},
             {"REBOOT_CTL_TOKEN", std::string(kToken)},
             {"REBOOT_SESSION", std::string(kSession)},
             {"REBOOT_ROLE", "client"}}};
}

[[nodiscard]] std::vector<u8> utf16(std::string_view text) {
    std::vector<u8> out;
    for (const char16_t unit : utf8_to_utf16(text)) {
        out.push_back(static_cast<u8>(unit));
        out.push_back(static_cast<u8>(unit >> 8));
    }
    return out;
}

[[nodiscard]] std::vector<u8> env_block(const ports::EnvBlock& env) {
    std::vector<u8> block = utf16("=C:=C:\\game");
    block.push_back(0);
    block.push_back(0);
    for (const auto& [name, value] : env.vars) {
        const std::vector<u8> entry = utf16(name + "=" + value);
        block.insert(block.end(), entry.begin(), entry.end());
        block.push_back(0);
        block.push_back(0);
    }
    block.push_back(0);
    block.push_back(0);
    return block;
}

// The engine's end of a game-control connection: preamble, then frames.
struct EngineEnd {
    std::unique_ptr<ports::IByteStream> stream;
    std::vector<u8> preamble;
    FrameLog frames{kGameControlFrameCap};
    bool closed = false;

    explicit EngineEnd(std::unique_ptr<ports::IByteStream> s) : stream(std::move(s)) {
        stream->on_read([this](std::span<const u8> bytes) {
            std::size_t used = 0;
            while (preamble.size() < kGameControlPreambleSize && used < bytes.size()) preamble.push_back(bytes[used++]);
            (void)frames.feed(bytes.subspan(used));
        });
        stream->on_close([this] { closed = true; });
    }

    template <ContractMessage T>
    void send(const T& message) {
        stream->write(encode_contract_frame(message));
    }
};

}  // namespace

TEST_CASE("the bootstrap reads from an environment and from a UTF-16 block", "[testing][game_control]") {
    const auto from_env = read_game_control_bootstrap(bootstrap_env());
    REQUIRE(from_env);
    CHECK(from_env->engine.to_string() == "127.0.0.1:41000");
    CHECK(to_hex(from_env->token) == kToken);
    CHECK(format_uuid(from_env->session.value) == kSession);
    CHECK(from_env->role == "client");

    const auto from_block = read_game_control_bootstrap(env_block(bootstrap_env()));
    REQUIRE(from_block);
    CHECK(from_block->token == from_env->token);

    ports::EnvBlock broken = bootstrap_env();
    broken.vars[2].second = "not-hex";
    const auto bad = read_game_control_bootstrap(broken);
    REQUIRE_FALSE(bad);
    CHECK(bad.error().id == "testing.bad_bootstrap");
    REQUIRE(bad.error().find_arg("name") != nullptr);
    CHECK(std::get<std::string>(*bad.error().find_arg("name")) == "REBOOT_CTL_TOKEN");
    CHECK_FALSE(read_game_control_bootstrap(ports::EnvBlock{}));
}

TEST_CASE("FakeClientDll takes a play session through Hello, Welcome and the default steps", "[testing][game_control]") {
    DeterministicRuntime runtime;
    auto [dll_end, engine_end] = make_memory_stream_pair(runtime.strand(), {}, {});
    EngineEnd engine(std::move(engine_end));
    FakeClientDllScript script;
    script.game = gc::GameBuild{"12.41", 12345};
    FakeClientDll dll(runtime.strand(), runtime.clock(), script);
    dll.attach(std::move(dll_end), *read_game_control_bootstrap(bootstrap_env()));
    runtime.run_until_idle();

    CHECK(parse_game_control_preamble(std::span<const u8, kGameControlPreambleSize>(engine.preamble.data(),
                                                                                       kGameControlPreambleSize)) ==
          gc::kPayloadAbi);
    const auto hello = engine.frames.last<gc::GcHello>();
    REQUIRE(hello);
    REQUIRE(*hello);
    CHECK(to_hex((*hello)->token) == kToken);
    CHECK((*hello)->role == gc::PeerRole::ClientDll);
    CHECK((*hello)->game.cl == 12345);

    gc::ClientDllConfig config;
    config.origin = "http://127.0.0.1:5000/s/key/";
    engine.send(gc::GcWelcome{config});
    engine.send(common::Ping{4});
    engine.send(gc::TestJoin{7, "127.0.0.1:7777"});
    runtime.run_until_idle();
    CHECK(dll.welcome()->origin == config.origin);
    CHECK(engine.frames.count(contract_frame_type_v<gc::Loaded>) == 1);
    CHECK(engine.frames.count(contract_frame_type_v<gc::RedirectReady>) == 1);
    CHECK(engine.frames.count(contract_frame_type_v<gc::LoggedIn>) == 1);
    CHECK(engine.frames.last<common::Pong>()->value().nonce == 4);
    CHECK(engine.frames.last<common::Unsupported>()->value().req_id == 7);
    CHECK(dll.pings_answered() == 1);
    CHECK(dll.test_joins().empty());

    engine.send(gc::GcShutdown{9});
    runtime.run_until_idle();
    CHECK(engine.frames.last<common::CommandResult>()->value().req_id == 9);
    CHECK(dll.closed());
    CHECK(engine.closed);
}

TEST_CASE("FakeClientDll scripts pauses, test mode and a hang", "[testing][game_control]") {
    DeterministicRuntime runtime;
    auto [dll_end, engine_end] = make_memory_stream_pair(runtime.strand(), {}, {});
    EngineEnd engine(std::move(engine_end));
    FakeClientDllScript script;
    script.token_override = std::array<u8, 32>{};
    script.hello_delay = 1s;
    script.after_welcome = {gc::Loaded{"fake", 2, gc::BuildMatch::Family}, ScriptPause{5s}, ScriptStopPonging{},
                            gc::LoggedIn{}};
    script.exit_on_shutdown = false;
    FakeClientDll dll(runtime.strand(), runtime.clock(), script);
    dll.attach(std::move(dll_end), *read_game_control_bootstrap(bootstrap_env()));
    runtime.run_until_idle();
    CHECK(engine.frames.count(contract_frame_type_v<gc::GcHello>) == 0);
    runtime.advance(1s);
    REQUIRE(engine.frames.last<gc::GcHello>());
    CHECK(engine.frames.last<gc::GcHello>()->value().token == std::array<u8, 32>{});

    gc::ClientDllConfig config;
    config.test_mode = true;
    engine.send(gc::GcWelcome{config});
    runtime.run_until_idle();
    CHECK(engine.frames.count(contract_frame_type_v<gc::Loaded>) == 1);
    CHECK(engine.frames.count(contract_frame_type_v<gc::LoggedIn>) == 0);
    runtime.advance(5s);
    CHECK(engine.frames.count(contract_frame_type_v<gc::LoggedIn>) == 1);

    engine.send(common::Ping{1});
    engine.send(gc::TestJoin{2, "10.0.0.1:7777"});
    engine.send(gc::TestQuit{3});
    engine.send(gc::GcShutdown{4});
    runtime.run_until_idle();
    CHECK(engine.frames.count(contract_frame_type_v<common::Pong>) == 0);
    CHECK(dll.test_joins() == std::vector<std::string>{"10.0.0.1:7777"});
    CHECK(dll.quit_requested());
    CHECK(dll.connected());
    const auto results = engine.frames.all<common::CommandResult>();
    REQUIRE(results);
    CHECK(results->size() == 3);
}

TEST_CASE("FakeClientDll reaches a real loopback listener", "[testing][game_control]") {
    DeterministicRuntime runtime;
    boost::asio::io_context io;
    auto work = boost::asio::make_work_guard(io);
    boost::asio::ip::tcp::acceptor acceptor(io, {boost::asio::ip::address_v4::loopback(), 0});
    std::thread io_thread([&io] { io.run(); });

    ports::EnvBlock env = bootstrap_env(acceptor.local_endpoint().port());
    FakeClientDll dll(runtime.strand(), runtime.clock(), {});
    boost::asio::ip::tcp::socket engine(io);
    std::thread accept_thread([&] { acceptor.accept(engine); });
    REQUIRE(dll.connect(io, *read_game_control_bootstrap(env)));
    accept_thread.join();
    runtime.run_until_idle();

    std::array<u8, kGameControlPreambleSize> preamble{};
    boost::asio::read(engine, boost::asio::buffer(preamble));
    CHECK(parse_game_control_preamble(preamble) == gc::kPayloadAbi);
    std::array<u8, 2> head{};
    boost::asio::read(engine, boost::asio::buffer(head));
    CHECK(head[0] == 0x44);
    dll.disconnect();
    work.reset();
    io.stop();
    io_thread.join();
}

TEST_CASE("a TCP stream destroyed by its own read callback hears nothing more", "[testing][game_control]") {
    boost::asio::io_context io;
    auto work = boost::asio::make_work_guard(io);
    boost::asio::ip::tcp::acceptor acceptor(io, {boost::asio::ip::address_v4::loopback(), 0});
    std::thread io_thread([&io] { io.run(); });
    boost::asio::ip::tcp::socket engine(io);
    std::thread accept_thread([&] { acceptor.accept(engine); });
    auto connected = connect_tcp(io, Endpoint{IpAddress::v4(0x7F000001), Port{acceptor.local_endpoint().port()}});
    accept_thread.join();
    REQUIRE(connected);

    // Two chunks wait in the stream before anyone reads, so destroying it mid-delivery leaves one.
    boost::asio::write(engine, boost::asio::buffer(std::string_view("a")));
    std::this_thread::sleep_for(100ms);
    boost::asio::write(engine, boost::asio::buffer(std::string_view("b")));
    std::this_thread::sleep_for(100ms);

    std::mutex mutex;
    std::condition_variable heard;
    int reads = 0;
    std::unique_ptr<ports::IByteStream> stream = std::move(*connected);
    ports::IByteStream* raw = stream.get();
    raw->on_read([&](std::span<const u8>) {
        stream.reset();
        const std::scoped_lock lock(mutex);
        ++reads;
        heard.notify_all();
    });
    {
        std::unique_lock lock(mutex);
        REQUIRE(heard.wait_for(lock, 5s, [&] { return reads > 0; }));
    }
    std::this_thread::sleep_for(100ms);
    {
        const std::scoped_lock lock(mutex);
        CHECK(reads == 1);
    }
    work.reset();
    io.stop();
    io_thread.join();
}

TEST_CASE("FakeWinhost spawns, injects, resumes into our DLL and dies with its connection", "[testing][game_control]") {
    DeterministicRuntime runtime;
    auto [host_end, engine_end] = make_memory_stream_pair(runtime.strand(), {}, {});
    EngineEnd engine(std::move(engine_end));
    FakeWinhostScript script;
    script.failing_injections = {"blocked.dll"};
    script.game_client_dll = FakeClientDllScript{};
    script.after_resume = {wh::Output{wh::ProcessRole::Game, wh::OutputStream::Stdout, {'h', 'i'}}};
    FakeWinhost winhost(runtime.strand(), runtime.clock(), script);

    ports::ProcessLaunch runner_launch;
    runner_launch.env = bootstrap_env();
    runner_launch.env.vars.back().second = "winhost";
    const auto bootstrap = FakeWinhost::bootstrap_of(runner_launch);
    REQUIRE(bootstrap);
    CHECK(bootstrap->role == "winhost");
    winhost.attach(std::move(host_end), *bootstrap);
    runtime.run_until_idle();
    REQUIRE(engine.frames.last<wh::WhHello>());

    wh::SpawnGame spawn;
    spawn.exe_utf16 = utf16("C:\\game\\FortniteClient-Win64-Shipping.exe");
    spawn.env_block_utf16 = env_block(bootstrap_env());
    spawn.companions = {wh::CompanionSpawn{utf16("C:\\game\\launcher.exe"), {}}};
    spawn.inject = {wh::InjectSpec{utf16("C:\\dlls\\rb_client.dll"), {}, wh::BootStrategy::EarlyBirdApc, wh::InjectPhase::Early},
                    wh::InjectSpec{utf16("C:\\dlls\\blocked.dll"), {}, wh::BootStrategy::EarlyBirdApc, wh::InjectPhase::Early},
                    wh::InjectSpec{utf16("C:\\dlls\\later.dll"), {}, wh::BootStrategy::AfterResume, wh::InjectPhase::LoggedIn}};
    engine.send(wh::WhWelcome{spawn});
    runtime.run_until_idle();
    const auto spawned = engine.frames.all<wh::Spawned>();
    REQUIRE(spawned);
    REQUIRE(spawned->size() == 2);
    CHECK((*spawned)[0].pid == 0x1000);
    CHECK((*spawned)[1].role == wh::ProcessRole::Companion);
    const auto injected = engine.frames.all<wh::Injected>();
    REQUIRE(injected);
    REQUIRE(injected->size() == 2);
    CHECK((*injected)[0].ok);
    CHECK_FALSE((*injected)[1].ok);
    CHECK((*injected)[1].error == kFakeInjectionError);
    CHECK(winhost.spawn_request()->companions.size() == 1);

    engine.send(wh::Resume{1});
    engine.send(wh::Inject{2, spawn.inject[2]});
    runtime.run_until_idle();
    CHECK(winhost.resumed());
    CHECK(winhost.injected().size() == 3);
    CHECK(engine.frames.count(contract_frame_type_v<wh::Output>) == 1);
    FakeClientDll* dll = winhost.game_client_dll();
    REQUIRE(dll != nullptr);
    CHECK_FALSE(dll->connected());

    auto [dll_end, dll_engine_end] = make_memory_stream_pair(runtime.strand(), {}, {});
    EngineEnd dll_engine(std::move(dll_engine_end));
    dll->attach(std::move(dll_end), *read_game_control_bootstrap(bootstrap_env()));
    runtime.run_until_idle();
    CHECK(dll_engine.frames.last<gc::GcHello>());

    // The engine's EOF ends the Job, and the game and its DLL with it.
    engine.stream->close();
    runtime.run_until_idle();
    CHECK(winhost.job_terminated());
    CHECK(dll->closed());
}

TEST_CASE("FakeWinhost answers Stop with Exited and goes", "[testing][game_control]") {
    DeterministicRuntime runtime;
    auto [host_end, engine_end] = make_memory_stream_pair(runtime.strand(), {}, {});
    EngineEnd engine(std::move(engine_end));
    FakeWinhostScript script;
    script.exit_code_on_stop = 3;
    FakeWinhost winhost(runtime.strand(), runtime.clock(), script);
    winhost.attach(std::move(host_end), *read_game_control_bootstrap(bootstrap_env()));
    engine.send(wh::WhWelcome{});
    engine.send(wh::Stop{5, 2000});
    runtime.run_until_idle();
    CHECK(winhost.stop_grace_ms() == 2000u);
    CHECK(engine.frames.last<common::CommandResult>()->value().req_id == 5);
    CHECK(engine.frames.last<wh::Exited>()->value().code == 3);
    CHECK(engine.closed);
    CHECK(winhost.job_terminated());
}

TEST_CASE("FakeSessionHost passes the session host suite", "[testing][conformance][game_control]") {
    DeterministicRuntime runtime;
    ManualWaiter waiter(runtime);
    FakeSessionHost host(runtime.strand());
    // Driven as games that spawn at launch; the first one exits by itself once resumed.
    host.on_launch([](FakeSessionControl& control) { control.spawn_all(0x3000); });
    host.on_resume([&host](FakeSessionControl& control) {
        control.emit(ports::Output{ports::SessionRole::Game, ports::OutputStream::Stdout, {'o', 'k'}});
        if (&control == host.sessions().front()) control.game_exits(0);
    });
    SessionHostConformanceSubject subject;
    subject.launch.exe = default_fake_root() / "game" / "reboot-fake-game.exe";
    subject.launch.env = bootstrap_env();
    subject.probe_dll = ports::InjectEntry{default_fake_root() / "probe.dll", {}, ports::BootStrategy::AfterResume,
                                           ports::InjectPhase::Early};
    subject.process_exists = [&host](u32) { return !host.last()->released(); };
    const ConformanceReport report = run_session_host_conformance(host, std::move(subject), {waiter, default_fake_root()});
    INFO(report.describe());
    CHECK(report.passed());
    REQUIRE(host.sessions().size() == 3);
    CHECK(host.sessions()[0]->injected().size() == 1);
    CHECK(host.sessions()[2]->released());
}

TEST_CASE("a session control stops sending once the engine dropped its session", "[testing][game_control]") {
    DeterministicRuntime runtime;
    FakeSessionHost host(runtime.strand());
    std::vector<ports::SessionHostEvent> events;
    ports::SessionLaunch launch;
    launch.companions = {ports::CompanionSpec{default_fake_root() / "eac.exe", {}}};
    auto session = host.launch(launch, [&events](ports::SessionHostEvent event) { events.push_back(std::move(event)); });
    REQUIRE(session);
    FakeSessionControl& control = *host.last();
    control.spawn_all(10);
    runtime.run_until_idle();
    REQUIRE(events.size() == 2);
    CHECK(std::get<ports::Spawned>(events[1]).pid == 11);
    CHECK_FALSE(control.bootstrap());

    host.faults().fail_next(SessionHostOperation::Resume, make_diag(ErrorDomain::Internal, MessageId{"internal.bug"}));
    CHECK_FALSE((*session)->resume());
    CHECK((*session)->resume());
    CHECK(control.resumed());
    (*session)->stop(2s);
    runtime.run_until_idle();
    CHECK(control.stop_grace() == 2s);
    CHECK(std::get<ports::Exited>(events.back()).code == kJobKillExitCode);
    session->reset();
    CHECK(control.released());
    control.game_exits(0);
    control.emit(ports::Spawned{ports::SessionRole::Game, 1});
    runtime.run_until_idle();
    CHECK(events.size() == 3);

    host.faults().fail_next(SessionHostOperation::Launch, make_diag(ErrorDomain::Internal, MessageId{"internal.bug"}));
    CHECK_FALSE(host.launch(launch, [](ports::SessionHostEvent) {}));
}
