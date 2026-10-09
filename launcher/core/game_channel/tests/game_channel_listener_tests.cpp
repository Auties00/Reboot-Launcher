#include <chrono>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <utility>
#include <variant>
#include <vector>

#include <boost/asio/executor_work_guard.hpp>
#include <boost/asio/io_context.hpp>
#include <catch2/catch_test_macros.hpp>

#include "game_channel_test_kit.hpp"
#include "reboot/contracts/common.hpp"
#include "reboot/contracts/winhost.hpp"
#include "reboot/game_channel/client_dll_peer.hpp"
#include "reboot/game_channel/game_channel_listener.hpp"
#include "reboot/game_channel/winhost_peer.hpp"
#include "reboot/testing/fake_client_dll.hpp"
#include "reboot/testing/fake_winhost.hpp"

namespace rb::game_channel {
namespace {

using namespace std::chrono_literals;
using test::gc::PeerRole;
using test::RawPeer;
using test::Rig;
namespace gc = contracts::game_client;
namespace wh = contracts::winhost;
namespace common = contracts::common;

constexpr std::string_view kModule = "rb_client.dll";

struct Record {
    std::optional<ClientDllHello> hello;
    std::optional<WinhostHello> winhost_hello;
    std::vector<GameLifecycleEvent> events;
    std::vector<WinhostEvent> winhost_events;
    std::vector<PeerLiveness> liveness;
    std::vector<Diagnostic> lost;
};

[[nodiscard]] gc::ClientDllConfig config(bool test_mode = false) {
    gc::ClientDllConfig out;
    out.origin = "http://127.0.0.1:4000/s/key/";
    out.sentinels = {"https://sentinel.example"};
    out.test_mode = test_mode;
    return out;
}

[[nodiscard]] ClientDllHandlers handlers(Record& record, Result<gc::ClientDllConfig> answer = config()) {
    return ClientDllHandlers{
        .configure = [&record, answer = std::move(answer)](const ClientDllHello& hello) -> Result<gc::ClientDllConfig> {
            record.hello = hello;
            return answer;
        },
        .on_event = [&record](GameLifecycleEvent event) { record.events.push_back(std::move(event)); },
        .on_liveness = [&record](PeerLiveness liveness) { record.liveness.push_back(liveness); },
        .on_lost = [&record](Diagnostic diag) { record.lost.push_back(std::move(diag)); },
    };
}

[[nodiscard]] wh::SpawnGame spawn_game() {
    wh::SpawnGame spawn;
    spawn.exe_utf16 = {'g', 0};
    spawn.argv_utf16 = {{'-', 0, 'x', 0}};
    spawn.env_block_utf16 = {0, 0};
    spawn.companions = {wh::CompanionSpawn{{'c', 0}, {}}};
    spawn.inject = {wh::InjectSpec{{'e', 0}, {}, wh::BootStrategy::EarlyBirdApc, wh::InjectPhase::Early}};
    return spawn;
}

[[nodiscard]] WinhostHandlers winhost_handlers(Record& record) {
    return WinhostHandlers{
        .configure = [&record](const WinhostHello& hello) -> Result<wh::SpawnGame> {
            record.winhost_hello = hello;
            return spawn_game();
        },
        .on_event = [&record](WinhostEvent event) { record.winhost_events.push_back(std::move(event)); },
        .on_liveness = [&record](PeerLiveness liveness) { record.liveness.push_back(liveness); },
        .on_lost = [&record](Diagnostic diag) { record.lost.push_back(std::move(diag)); },
    };
}

[[nodiscard]] gc::GcHello hello_for(const ControlToken& token, u32 protocol = gc::kPayloadAbi) {
    gc::GcHello hello;
    hello.token = test::token_bytes(token);
    hello.role = PeerRole::ClientDll;
    hello.dll_build = "raw";
    hello.protocol = protocol;
    return hello;
}

[[nodiscard]] std::unique_ptr<ClientDllPeer> open(Rig& rig, SessionId session, Record& record,
                                                  Result<gc::ClientDllConfig> answer = config(),
                                                  RunnerMultiplier multiplier = RunnerMultiplier::Native) {
    auto peer = rig.listener.open_client_dll(session, std::string(kModule), multiplier, handlers(record, std::move(answer)));
    REQUIRE(peer);
    return std::move(*peer);
}

// A raw peer that has sent the preamble and a valid Hello and been welcomed.
[[nodiscard]] RawPeer welcomed_raw(Rig& rig, const ClientDllPeer& peer) {
    RawPeer raw(rig.connect());
    raw.preamble();
    raw.send(hello_for(peer.token()));
    rig.run();
    REQUIRE(peer.welcomed());
    return raw;
}

[[nodiscard]] ReplyHandler into(std::optional<Result<void>>& slot) {
    return [&slot](Result<void> outcome) { slot = std::move(outcome); };
}

[[nodiscard]] std::string arg_text(const Diagnostic& diag, std::string_view name) {
    const Arg* arg = diag.find_arg(name);
    if (arg == nullptr) return {};
    if (const auto* text = std::get_if<std::string>(arg)) return *text;
    if (const auto* number = std::get_if<u64>(arg)) return std::to_string(*number);
    return {};
}

TEST_CASE("a client DLL with its token is welcomed and its events reach the session", "[game_channel][client_dll]") {
    Rig rig;
    Record record;
    const SessionId session = rig.new_session();
    auto peer = open(rig, session, record);
    testing::FakeClientDllScript script;
    script.dll_build = "dll-7";
    script.game = gc::GameBuild{"12.41", 12345};
    testing::FakeClientDll dll(rig.strand, rig.clock, std::move(script));
    dll.attach(rig.connect(), test::bootstrap_for(peer->token(), session));
    CHECK_FALSE(peer->welcomed());
    rig.run();

    CHECK(peer->welcomed());
    REQUIRE(record.hello);
    CHECK(record.hello->dll_build == "dll-7");
    CHECK(record.hello->game.version == "12.41");
    CHECK(record.hello->game.cl == 12345);
    REQUIRE(dll.welcome());
    CHECK(dll.welcome()->origin == "http://127.0.0.1:4000/s/key/");
    CHECK(dll.welcome()->sentinels == std::vector<std::string>{"https://sentinel.example"});
    REQUIRE(record.events.size() == 3);
    CHECK(std::holds_alternative<Loaded>(record.events[0]));
    CHECK(std::holds_alternative<RedirectReady>(record.events[1]));
    CHECK(std::holds_alternative<LoggedIn>(record.events[2]));
    CHECK(record.lost.empty());
}

TEST_CASE("every client DLL event maps onto the lifecycle", "[game_channel][client_dll]") {
    Rig rig;
    Record record;
    const SessionId session = rig.new_session();
    auto peer = open(rig, session, record);
    testing::FakeClientDllScript script;
    script.after_welcome = {gc::PatchResult{"memory", gc::PatchStatus::Applied, 0x10}, gc::HookFailed{"console", false},
                            gc::WindowCreated{0x42}, gc::ConsoleReady{}, gc::ExitRequested{gc::ExitKind::RequestExit, 3, "lobby"},
                            gc::Fatal{"redirect"}};
    testing::FakeClientDll dll(rig.strand, rig.clock, std::move(script));
    dll.attach(rig.connect(), test::bootstrap_for(peer->token(), session));
    rig.run();
    dll.send(gc::TravelStarted{});
    dll.send(gc::TravelEnded{});
    dll.send(gc::Joined{"10.0.0.1:7777"});
    dll.send(gc::Disconnected{"timed out"});
    rig.run();

    REQUIRE(record.events.size() == 10);
    CHECK(std::get<PatchResult>(record.events[0]).rva == 0x10);
    CHECK(std::get<HookFailed>(record.events[1]).step == "console");
    CHECK(std::get<WindowCreated>(record.events[2]).hwnd == 0x42);
    CHECK(std::holds_alternative<ConsoleReady>(record.events[3]));
    CHECK(std::get<ExitRequested>(record.events[4]).phase == "lobby");
    const auto& fatal = std::get<SessionFatal>(record.events[5]);
    CHECK(fatal.cause == FatalCause::DllStep);
    CHECK(fatal.step == "redirect");
    CHECK(std::holds_alternative<TravelStarted>(record.events[6]));
    CHECK(std::holds_alternative<TravelEnded>(record.events[7]));
    CHECK(std::get<Joined>(record.events[8]).address == "10.0.0.1:7777");
    CHECK(std::get<Disconnected>(record.events[9]).reason == "timed out");
}

TEST_CASE("Shutdown gets the DLL's answer and the disconnect after it is peer_lost", "[game_channel][client_dll]") {
    Rig rig;
    Record record;
    const SessionId session = rig.new_session();
    auto peer = open(rig, session, record);
    testing::FakeClientDll dll(rig.strand, rig.clock, testing::FakeClientDllScript{});
    dll.attach(rig.connect(), test::bootstrap_for(peer->token(), session));
    rig.run();

    std::optional<Result<void>> reply;
    REQUIRE(peer->shutdown(into(reply)));
    rig.run();
    REQUIRE(reply);
    CHECK(reply->has_value());
    REQUIRE(record.lost.size() == 1);
    CHECK(record.lost[0].id == "game_channel.peer_lost");
    CHECK(arg_text(record.lost[0], "role") == "client_dll");
}

TEST_CASE("requests before Welcome fail at once with not_welcomed", "[game_channel][client_dll]") {
    Rig rig;
    Record record;
    auto peer = open(rig, rig.new_session(), record, config(true));
    std::optional<Result<void>> reply;
    const auto shutdown = peer->shutdown(into(reply));
    REQUIRE_FALSE(shutdown);
    CHECK(shutdown.error().id == "game_channel.not_welcomed");
    const auto join = peer->test_join("1.2.3.4:7777", into(reply));
    REQUIRE_FALSE(join);
    CHECK(join.error().id == "game_channel.not_welcomed");
    rig.run();
    CHECK_FALSE(reply);
}

TEST_CASE("test requests need test mode", "[game_channel][client_dll]") {
    Rig rig;
    Record off_record;
    Record on_record;
    const SessionId off_session = rig.new_session();
    const SessionId on_session = rig.new_session();
    auto off = open(rig, off_session, off_record, config(false));
    auto on = open(rig, on_session, on_record, config(true));
    testing::FakeClientDll off_dll(rig.strand, rig.clock, testing::FakeClientDllScript{});
    testing::FakeClientDll on_dll(rig.strand, rig.clock, testing::FakeClientDllScript{});
    off_dll.attach(rig.connect(), test::bootstrap_for(off->token(), off_session));
    on_dll.attach(rig.connect(), test::bootstrap_for(on->token(), on_session));
    rig.run();

    std::optional<Result<void>> reply;
    const auto refused = off->test_quit(into(reply));
    REQUIRE_FALSE(refused);
    CHECK(refused.error().id == "game_channel.test_mode_off");
    CHECK(arg_text(refused.error(), "request") == "test_quit");

    std::optional<Result<void>> joined;
    std::optional<Result<void>> quit;
    REQUIRE(on->test_join("10.0.0.2:7777", into(joined)));
    REQUIRE(on->test_quit(into(quit)));
    rig.run();
    REQUIRE(joined);
    CHECK(joined->has_value());
    REQUIRE(quit);
    CHECK(quit->has_value());
    CHECK(on_dll.test_joins() == std::vector<std::string>{"10.0.0.2:7777"});
    CHECK(on_dll.quit_requested());
}

TEST_CASE("Unsupported and a failed CommandResult reach the request's handler", "[game_channel][client_dll]") {
    Rig rig;
    Record record;
    auto peer = open(rig, rig.new_session(), record);
    RawPeer raw = welcomed_raw(rig, *peer);

    std::optional<Result<void>> first;
    std::optional<Result<void>> second;
    REQUIRE(peer->shutdown(into(first)));
    REQUIRE(peer->shutdown(into(second)));
    rig.run();
    const auto requests = raw.received().all<gc::GcShutdown>();
    REQUIRE(requests);
    REQUIRE(requests->size() == 2);
    CHECK((*requests)[0].req_id != (*requests)[1].req_id);

    raw.send(common::Unsupported{(*requests)[0].req_id});
    const Diagnostic cause = make_diag(ErrorDomain::GameChannel, MessageId{"game_channel.request_failed"})
                                 .arg("role", "client_dll")
                                 .arg("request", "shutdown")
                                 .build();
    raw.send(common::CommandResult{(*requests)[1].req_id, false, common::to_wire(cause)});
    raw.send(common::CommandResult{999, true, std::nullopt});
    rig.run();

    REQUIRE(first);
    REQUIRE_FALSE(first->has_value());
    CHECK(first->error().id == "game_channel.unsupported_request");
    CHECK(arg_text(first->error(), "request") == "shutdown");
    REQUIRE(second);
    REQUIRE_FALSE(second->has_value());
    CHECK(second->error().id == "game_channel.request_failed");
    REQUIRE(second->error().causes.size() == 1);
    CHECK(second->error().causes[0].id == "game_channel.request_failed");
    CHECK(record.lost.empty());
}

TEST_CASE("a lost connection fails pending requests before on_lost, and later ones on the strand",
          "[game_channel][client_dll]") {
    Rig rig;
    Record record;
    auto peer = open(rig, rig.new_session(), record);
    RawPeer raw = welcomed_raw(rig, *peer);

    std::vector<std::string> order;
    std::optional<Result<void>> pending;
    REQUIRE(peer->shutdown([&](Result<void> outcome) {
        order.emplace_back("reply");
        CHECK(record.lost.empty());
        pending = std::move(outcome);
    }));
    raw.disconnect();
    rig.run();
    REQUIRE(pending);
    REQUIRE_FALSE(pending->has_value());
    CHECK(pending->error().id == "game_channel.peer_lost");
    REQUIRE(record.lost.size() == 1);
    CHECK(record.lost[0].id == "game_channel.peer_lost");

    std::optional<Result<void>> late;
    REQUIRE(peer->shutdown(into(late)));
    CHECK_FALSE(late);
    rig.run();
    REQUIRE(late);
    CHECK(late->error().id == "game_channel.peer_lost");
    CHECK(record.lost.size() == 1);
}

TEST_CASE("Hellos the channel refuses before Welcome", "[game_channel][client_dll]") {
    Rig rig;
    Record record;
    auto peer = open(rig, rig.new_session(), record);

    SECTION("a bad preamble is closed and the token stays usable") {
        RawPeer raw(rig.connect());
        const std::array<u8, 8> junk{'G', 'E', 'T', ' ', '/', ' ', 'H', 'T'};
        raw.write(junk);
        rig.run();
        CHECK(raw.closed());
        CHECK(record.lost.empty());
        RawPeer good = welcomed_raw(rig, *peer);
        CHECK(record.lost.empty());
    }
    SECTION("another payload ABI refuses the peer") {
        RawPeer raw(rig.connect());
        raw.preamble(static_cast<u16>(gc::kPayloadAbi + 1));
        raw.send(hello_for(peer->token()));
        rig.run();
        CHECK(raw.closed());
        CHECK_FALSE(peer->welcomed());
        REQUIRE(record.lost.size() == 1);
        CHECK(record.lost[0].id == "game_channel.payload_abi_mismatch");
        CHECK(arg_text(record.lost[0], "actual") == std::to_string(gc::kPayloadAbi + 1));
        CHECK(arg_text(record.lost[0], "expected") == std::to_string(gc::kPayloadAbi));
    }
    SECTION("another protocol refuses the peer") {
        RawPeer raw(rig.connect());
        raw.preamble();
        raw.send(hello_for(peer->token(), 99));
        rig.run();
        CHECK(raw.closed());
        REQUIRE(record.lost.size() == 1);
        CHECK(record.lost[0].id == "game_channel.protocol_mismatch");
        CHECK(arg_text(record.lost[0], "actual") == "99");
    }
    SECTION("a configure error refuses the peer with that error") {
        Record refused_record;
        const SessionId session = rig.new_session();
        auto refused = open(rig, session, refused_record,
                            std::unexpected(make_diag(ErrorDomain::Play, MessageId{"play.no_route"}).build()));
        testing::FakeClientDll dll(rig.strand, rig.clock, testing::FakeClientDllScript{});
        dll.attach(rig.connect(), test::bootstrap_for(refused->token(), session));
        rig.run();
        CHECK(dll.closed());
        CHECK_FALSE(dll.welcome());
        REQUIRE(refused_record.lost.size() == 1);
        CHECK(refused_record.lost[0].id == "play.no_route");
    }
    SECTION("an unknown token is closed and nobody is told") {
        RawPeer raw(rig.connect());
        raw.preamble();
        gc::GcHello hello = hello_for(peer->token());
        hello.token.fill(0x5A);
        raw.send(hello);
        rig.run();
        CHECK(raw.closed());
        CHECK(record.lost.empty());
    }
    SECTION("a winhost Hello with a client DLL token is a role mismatch") {
        RawPeer raw(rig.connect());
        raw.preamble();
        raw.send(wh::WhHello{test::token_bytes(peer->token()), "wh", wh::kWinhostProtocol, 1});
        rig.run();
        CHECK(raw.closed());
        CHECK(record.lost.empty());
        RawPeer good = welcomed_raw(rig, *peer);
    }
    SECTION("a first frame that is no Hello is closed") {
        RawPeer raw(rig.connect());
        raw.preamble();
        raw.send(common::Ping{1});
        rig.run();
        CHECK(raw.closed());
        CHECK(record.lost.empty());
    }
    SECTION("a second connection with a claimed token is closed and the session is not told") {
        RawPeer first = welcomed_raw(rig, *peer);
        RawPeer second(rig.connect());
        second.preamble();
        second.send(hello_for(peer->token()));
        rig.run();
        CHECK(second.closed());
        CHECK_FALSE(first.closed());
        CHECK(record.lost.empty());
        CHECK(peer->welcomed());
    }
}

TEST_CASE("a connection that sends no Hello in time is closed", "[game_channel][client_dll]") {
    Rig rig;
    Record record;
    auto peer = open(rig, rig.new_session(), record, config(), RunnerMultiplier::Wine);
    RawPeer raw(rig.connect());
    raw.preamble();
    rig.run();
    rig.advance(3s);
    CHECK_FALSE(raw.closed());
    rig.advance(1s);
    rig.run();
    CHECK(raw.closed());
    CHECK(record.lost.empty());
}

TEST_CASE("a protocol break after Welcome is peer_lost with its cause", "[game_channel][client_dll]") {
    Rig rig;
    Record record;
    auto peer = open(rig, rig.new_session(), record);
    RawPeer raw = welcomed_raw(rig, *peer);

    SECTION("a frame type no client DLL sends") {
        raw.send(wh::Spawned{wh::ProcessRole::Game, 1});
    }
    SECTION("a frame that does not decode") {
        // Loaded (0x410) whose one payload byte is a field key of 0.
        const std::array<u8, 4> broken{0x44, 0x10, 0x01, 0x00};
        raw.write(broken);
    }
    rig.run();
    CHECK(raw.closed());
    REQUIRE(record.lost.size() == 1);
    CHECK(record.lost[0].id == "game_channel.peer_lost");
    REQUIRE(record.lost[0].causes.size() == 1);
    CHECK(record.lost[0].causes[0].id == "contracts.malformed_frame");
}

TEST_CASE("the peer's Ping is answered and its Log is accepted", "[game_channel][client_dll]") {
    Rig rig;
    Record record;
    auto peer = open(rig, rig.new_session(), record);
    RawPeer raw = welcomed_raw(rig, *peer);
    raw.send(common::Ping{42});
    raw.send(common::Log{LogLevel::Info, 3, "hello\nforged line"});
    rig.run();
    const auto pongs = raw.received().all<common::Pong>();
    REQUIRE(pongs);
    REQUIRE(pongs->size() == 1);
    CHECK((*pongs)[0].nonce == 42);
    CHECK(record.lost.empty());
}

TEST_CASE("a peer whose Pongs stop is reported Unresponsive once", "[game_channel][liveness]") {
    Rig rig;
    testing::FakeClientDllScript script;
    script.after_welcome = {testing::ScriptStopPonging{}};
    const SessionId session = rig.new_session();
    Record dll_record;
    auto hung = open(rig, session, dll_record);
    testing::FakeClientDll dll(rig.strand, rig.clock, std::move(script));
    dll.attach(rig.connect(), test::bootstrap_for(hung->token(), session));
    rig.run();
    REQUIRE(hung->welcomed());
    for (int i = 0; i < 3; ++i) rig.advance(kLivenessPingInterval);
    rig.run();
    CHECK(dll_record.liveness.empty());
    rig.advance(kLivenessPingInterval);
    rig.run();
    CHECK(dll_record.liveness == std::vector<PeerLiveness>{PeerLiveness::Unresponsive});
    rig.advance(kLivenessPingInterval * 3);
    rig.run();
    CHECK(dll_record.liveness.size() == 1);
    CHECK(dll_record.lost.empty());
}

TEST_CASE("a Pong whose tick stands still counts as missed", "[game_channel][liveness]") {
    Rig rig;
    Record record;
    auto peer = open(rig, rig.new_session(), record);
    RawPeer raw = welcomed_raw(rig, *peer);
    const auto answer = [&](u64 tick) {
        rig.advance(kLivenessPingInterval);
        rig.run();
        raw.send(common::Pong{tick});
        rig.run();
    };
    answer(7);
    answer(7);
    answer(7);
    answer(7);
    CHECK(record.liveness.empty());
    answer(7);
    CHECK(record.liveness == std::vector<PeerLiveness>{PeerLiveness::Unresponsive});
    raw.send(common::Pong{8});
    rig.run();
    CHECK(record.liveness == std::vector<PeerLiveness>{PeerLiveness::Unresponsive, PeerLiveness::Responsive});
    const auto pings = raw.received().all<common::Ping>();
    REQUIRE(pings);
    REQUIRE(pings->size() == 5);
    CHECK((*pings)[4].nonce > (*pings)[0].nonce);
}

TEST_CASE("dropping a peer closes its connection, revokes its token and tells nobody", "[game_channel][client_dll]") {
    Rig rig;
    Record record;
    const SessionId session = rig.new_session();
    auto peer = open(rig, session, record);
    const std::array<u8, 32> token = test::token_bytes(peer->token());
    RawPeer raw = welcomed_raw(rig, *peer);
    peer.reset();
    rig.run();
    CHECK(raw.closed());
    CHECK(record.lost.empty());
    const auto claimed = rig.tokens.claim(token, PeerRole::ClientDll);
    REQUIRE_FALSE(claimed);
    CHECK(claimed.error().id == "game_channel.unknown_token");
    // The key is free again.
    Record next;
    auto reopened = open(rig, session, next);
    CHECK(reopened);
}

TEST_CASE("a handler may drop its peer while frames are still queued", "[game_channel][client_dll]") {
    Rig rig;
    const SessionId session = rig.new_session();
    std::unique_ptr<ClientDllPeer> peer;
    int events = 0;
    ClientDllHandlers dropping{
        .configure = [](const ClientDllHello&) -> Result<gc::ClientDllConfig> { return config(); },
        .on_event = [&](GameLifecycleEvent) {
            ++events;
            peer.reset();
        },
        .on_liveness = {},
        .on_lost = [](Diagnostic) { FAIL("dropped peers are never told"); },
    };
    auto opened = rig.listener.open_client_dll(session, std::string(kModule), RunnerMultiplier::Native, std::move(dropping));
    REQUIRE(opened);
    peer = std::move(*opened);
    testing::FakeClientDll dll(rig.strand, rig.clock, testing::FakeClientDllScript{});
    dll.attach(rig.connect(), test::bootstrap_for(peer->token(), session));
    rig.run();
    CHECK(events == 1);
    CHECK_FALSE(peer);
    CHECK(dll.closed());
}

TEST_CASE("one key opens one peer", "[game_channel][client_dll]") {
    Rig rig;
    Record record;
    const SessionId session = rig.new_session();
    auto peer = open(rig, session, record);
    Record other;
    const auto again = rig.listener.open_client_dll(session, std::string(kModule), RunnerMultiplier::Native, handlers(other));
    REQUIRE_FALSE(again);
    CHECK(again.error().id == "game_channel.duplicate_peer");
    CHECK(rig.listener.open_client_dll(session, "custom.dll", RunnerMultiplier::Native, handlers(other)));
}

TEST_CASE("close ends every connection and refuses later ones", "[game_channel][listener]") {
    Rig rig;
    Record record;
    auto peer = open(rig, rig.new_session(), record);
    RawPeer raw = welcomed_raw(rig, *peer);
    rig.listener.close();
    rig.run();
    CHECK(raw.closed());
    REQUIRE(record.lost.size() == 1);
    CHECK(record.lost[0].id == "game_channel.peer_lost");

    RawPeer late(rig.connect());
    rig.run();
    CHECK(late.closed());
}

TEST_CASE("ctl_url needs a listening socket", "[game_channel][listener]") {
    Rig rig;
    const auto url = rig.listener.ctl_url();
    REQUIRE_FALSE(url);
    CHECK(url.error().id == "game_channel.not_listening");
}

TEST_CASE("winhost is welcomed with SpawnGame and serves Resume, Inject and Stop", "[game_channel][winhost]") {
    Rig rig;
    Record record;
    const SessionId session = rig.new_session();
    auto opened = rig.listener.open_winhost(session, RunnerMultiplier::Wine, winhost_handlers(record));
    REQUIRE(opened);
    std::unique_ptr<WinhostPeer> peer = std::move(*opened);
    testing::FakeWinhostScript script;
    script.build = "wh-3";
    testing::FakeWinhost host(rig.strand, rig.clock, std::move(script));
    testing::GameControlBootstrap bootstrap = test::bootstrap_for(peer->token(), session);
    bootstrap.role = "winhost";
    host.attach(rig.connect(), bootstrap);

    std::optional<Result<void>> early;
    const auto refused = peer->resume(into(early));
    REQUIRE_FALSE(refused);
    CHECK(refused.error().id == "game_channel.not_welcomed");
    rig.run();

    REQUIRE(peer->welcomed());
    REQUIRE(record.winhost_hello);
    CHECK(record.winhost_hello->build == "wh-3");
    REQUIRE(host.spawn_request());
    CHECK(host.spawn_request()->exe_utf16 == spawn_game().exe_utf16);
    CHECK(host.spawn_request()->argv_utf16 == spawn_game().argv_utf16);
    REQUIRE(record.winhost_events.size() == 3);
    CHECK(std::get<wh::Spawned>(record.winhost_events[0]).role == wh::ProcessRole::Game);
    CHECK(std::get<wh::Spawned>(record.winhost_events[1]).role == wh::ProcessRole::Companion);
    CHECK(std::get<wh::Injected>(record.winhost_events[2]).ok);

    std::optional<Result<void>> resumed;
    std::optional<Result<void>> injected;
    REQUIRE(peer->resume(into(resumed)));
    REQUIRE(peer->inject(wh::InjectSpec{{'l', 0}, {}, wh::BootStrategy::AfterResume, wh::InjectPhase::LoggedIn},
                         into(injected)));
    rig.run();
    REQUIRE(resumed);
    CHECK(resumed->has_value());
    REQUIRE(injected);
    CHECK(injected->has_value());
    CHECK(host.resumed());
    CHECK(host.injected().size() == 2);

    std::optional<Result<void>> stopped;
    REQUIRE(peer->stop(1500ms, into(stopped)));
    rig.run();
    REQUIRE(stopped);
    CHECK(stopped->has_value());
    CHECK(host.stop_grace_ms() == 1500u);
    REQUIRE(std::holds_alternative<wh::Exited>(record.winhost_events.back()));
    REQUIRE(record.lost.size() == 1);
    CHECK(record.lost[0].id == "game_channel.peer_lost");
    CHECK(arg_text(record.lost[0], "role") == "winhost");
}

TEST_CASE("winhost with another protocol is refused", "[game_channel][winhost]") {
    Rig rig;
    Record record;
    const SessionId session = rig.new_session();
    auto opened = rig.listener.open_winhost(session, RunnerMultiplier::Wine, winhost_handlers(record));
    REQUIRE(opened);
    testing::FakeWinhostScript script;
    script.protocol = wh::kWinhostProtocol + 1;
    testing::FakeWinhost host(rig.strand, rig.clock, std::move(script));
    testing::GameControlBootstrap bootstrap = test::bootstrap_for((*opened)->token(), session);
    bootstrap.role = "winhost";
    host.attach(rig.connect(), bootstrap);
    rig.run();
    CHECK_FALSE(record.winhost_hello);
    REQUIRE(record.lost.size() == 1);
    CHECK(record.lost[0].id == "game_channel.protocol_mismatch");
    CHECK(arg_text(record.lost[0], "role") == "winhost");
}

TEST_CASE("a client DLL reaches the listener over loopback TCP", "[game_channel][listener][tcp]") {
    ManualClock clock;
    test::TestStrand strand{clock};
    TimerService timers{clock, strand};
    testing::FakeRandom random{11};
    Redactor redactor;
    TokenRegistry tokens{random, redactor};
    boost::asio::io_context io;
    auto work = boost::asio::make_work_guard(io);
    std::thread io_thread([&io] { io.run(); });
    {
        GameChannelListener listener{io, strand, timers, tokens};
        const auto port = listener.start();
        REQUIRE(port);
        CHECK(port->value != 0);
        const auto again = listener.start();
        REQUIRE(again);
        CHECK(*again == *port);
        const auto url = listener.ctl_url();
        REQUIRE(url);
        CHECK(*url == "tcp://127.0.0.1:" + std::to_string(port->value));

        Record record;
        const SessionId session{uuid_v4(random)};
        auto opened = listener.open_client_dll(session, std::string(kModule), RunnerMultiplier::Native, handlers(record));
        REQUIRE(opened);
        std::unique_ptr<ClientDllPeer> peer = std::move(*opened);
        testing::GameControlBootstrap bootstrap = test::bootstrap_for(peer->token(), session);
        bootstrap.engine = Endpoint{IpAddress::v4(0x7F000001), *port};
        testing::FakeClientDll dll(strand, clock, testing::FakeClientDllScript{});
        REQUIRE(dll.connect(io, bootstrap));
        strand.run_until([&] { return record.events.size() == 3; });
        CHECK(peer->welcomed());
        REQUIRE(dll.welcome());

        std::optional<Result<void>> reply;
        REQUIRE(peer->shutdown(into(reply)));
        strand.run_until([&] { return reply.has_value() && !record.lost.empty(); });
        CHECK(reply->has_value());
        CHECK(record.lost[0].id == "game_channel.peer_lost");
        peer.reset();
        listener.close();
        CHECK_FALSE(listener.ctl_url());
    }
    work.reset();
    io.stop();
    io_thread.join();
    strand.drain();
}

}  // namespace
}  // namespace rb::game_channel
