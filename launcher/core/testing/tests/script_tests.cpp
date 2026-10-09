#include <chrono>
#include <string>

#include <catch2/catch_message.hpp>
#include <catch2/catch_test_macros.hpp>

#include "reboot/contracts/game_client.hpp"
#include "reboot/contracts/game_server.hpp"
#include "reboot/testing/fake_backend_script.hpp"
#include "reboot/testing/fake_game_script.hpp"
#include "reboot/testing/fake_game_server_script.hpp"

using namespace reboot;
using namespace reboot::testing;
using namespace std::chrono_literals;
namespace gc = contracts::game_client;
namespace gs = contracts::game_server;
namespace be = contracts::backend;

TEST_CASE("an empty script is the conforming default", "[testing][scripts]") {
    const auto backend = parse_fake_backend_script("{}");
    REQUIRE(backend);
    CHECK(to_json(*backend) == to_json(FakeBackendScript{}));
    const auto server = parse_fake_game_server_script("{}");
    REQUIRE(server);
    CHECK(server->description.build == "fake");
    const auto game = parse_fake_game_script("{}");
    REQUIRE(game);
    CHECK(game->client_dll.after_welcome.size() == 3);
}

TEST_CASE("backend scripts round-trip through JSON", "[testing][scripts]") {
    FakeBackendScript script;
    script.child.protocol = 7;
    script.child.hello_delay = 1500ms;
    script.child.crash = ScriptedFailure{ScriptStage::Welcome, 2s};
    script.child.unsupported = {0x220, 0x222};
    script.child.stderr_lines = {"line one", "line \"two\""};
    script.child.garbage_frame = 0x202;
    script.ready_delay = 250ms;
    script.serve_http = true;
    script.content = {2, 1234567890123};
    script.match_target_requests = {be::ResolveMatchTarget{9, "acct", "playlist_defaultsolo"}};
    script.logins = {be::LoginObserved{"acct", "key"}};
    const std::string json = to_json(script);
    CHECK(json.find("\"hello_delay_ms\":1500") != std::string::npos);
    CHECK(json.find("\"stage\":\"welcome\"") != std::string::npos);
    const auto parsed = parse_fake_backend_script(json);
    REQUIRE(parsed);
    CHECK(to_json(*parsed) == json);
    CHECK(parsed->child.crash->after == 2s);
    CHECK(parsed->content.serial == 1234567890123u);
}

TEST_CASE("game server scripts round-trip variants and descriptions", "[testing][scripts]") {
    FakeGameServerScript script;
    script.description.sockets.push_back(gs::SocketSpec{gs::SocketRole::Beacon});
    script.description.supports.push_back(gs::VersionSupport{"12.41", "12.41", {gs::ClRange{100, 200}}});
    script.hello_description = default_fake_description();
    script.events = {TimedServerEvent{1s, gs::StateChanged{gs::MatchState::Warmup}},
                     TimedServerEvent{2s, gs::MatchEnded{gs::MatchEndReason::Aborted, "winner", {gs::Placement{"a", 1}}}},
                     TimedServerEvent{3s, contracts::common::Log{LogLevel::Warn, 4, "careful"}}};
    script.match_length = 90s;
    script.bind_failure_index = 1;
    const std::string json = to_json(script);
    CHECK(json.find("{\"state_changed\":{\"state\":\"warmup\"}}") != std::string::npos);
    const auto parsed = parse_fake_game_server_script(json);
    REQUIRE(parsed);
    CHECK(to_json(*parsed) == json);
    CHECK(std::get<gs::MatchEnded>(parsed->events[1].event).winner == "winner");
}

TEST_CASE("game scripts round-trip client DLL steps, bare ones included", "[testing][scripts]") {
    FakeGameScript script;
    script.act_as_client_dll = false;
    script.client_dll.exe_sha256[0] = 0xAB;
    script.client_dll.token_override = std::array<u8, 32>{};
    script.client_dll.after_welcome = {ScriptPause{3s}, ScriptStopPonging{}, gc::PatchResult{"memory", gc::PatchStatus::NotFound, 0x1234},
                                       gc::ExitRequested{gc::ExitKind::FatalError, -1, "loading"}, ScriptDisconnect{}};
    script.expect_modules = {"rb_client.dll"};
    script.output_lines = {"LogInit: Display: Engine is initialized."};
    script.exit_after = 5s;
    script.exit_code = -3;
    const std::string json = to_json(script);
    CHECK(json.find("\"stop_ponging\"") != std::string::npos);
    CHECK(json.find("\"exe_sha256\":\"ab00") != std::string::npos);
    const auto parsed = parse_fake_game_script(json);
    REQUIRE(parsed);
    CHECK(to_json(*parsed) == json);
    CHECK(std::holds_alternative<ScriptDisconnect>(parsed->client_dll.after_welcome.back()));
    CHECK(parsed->exit_code == -3);
}

TEST_CASE("a script with a typo or a wrong type fails testing.bad_script", "[testing][scripts]") {
    for (const char* bad : {R"({"serve_htp": true})", R"({"child": {"crash": {"stage": "later"}}})",
                            R"({"ready_delay_ms": "soon"})", R"({"ready_delay": 5})", "not json", "[]",
                            R"({"content": {"schema": -1}})"}) {
        const auto parsed = parse_fake_backend_script(bad);
        INFO(bad);
        REQUIRE_FALSE(parsed);
        CHECK(parsed.error().id == "testing.bad_script");
    }
    const auto variant = parse_fake_game_script(R"({"client_dll": {"after_welcome": ["loaded"]}})");
    REQUIRE_FALSE(variant);
    CHECK(parse_fake_game_script(R"({"client_dll": {"after_welcome": ["logged_in", {"pause": {"duration_ms": 10}}]}})"));
    CHECK(load_fake_backend_script(NativePath("no/such/script.json")).error().kind == ErrorKind::NotFound);
}
