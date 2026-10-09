// Defaults and the JSON form of the fake scripts.
#include <array>
#include <fstream>
#include <iterator>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

#include "messages.hpp"
#include "reboot/contracts/common.hpp"
#include "reboot/contracts/game_client.hpp"
#include "reboot/contracts/game_server.hpp"
#include "reboot/foundation/log.hpp"
#include "reboot/testing/fake_backend_script.hpp"
#include "reboot/testing/fake_client_dll_script.hpp"
#include "reboot/testing/fake_game_script.hpp"
#include "reboot/testing/fake_game_server_script.hpp"
#include "script_json.hpp"

namespace reboot::testing::script_json {

namespace gc = contracts::game_client;
namespace gs = contracts::game_server;

template <class E>
using Of = std::type_identity<E>;
using NameList = std::span<const std::string_view>;

// Enumerator order; the JSON names are their snake_case forms.
inline constexpr std::array<std::string_view, 3> kScriptStages{"start", "welcome", "ready"};
inline constexpr std::array<std::string_view, 5> kLogLevels{"trace", "debug", "info", "warn", "error"};
inline constexpr std::array<std::string_view, 3> kBuildMatches{"exact", "family", "none"};
inline constexpr std::array<std::string_view, 4> kPatchStatuses{"applied", "skipped", "not_found", "failed"};
inline constexpr std::array<std::string_view, 3> kExitKinds{"request_exit", "fatal_error", "crash"};
inline constexpr std::array<std::string_view, 4> kMatchStates{"lobby", "warmup", "in_progress", "ending"};
inline constexpr std::array<std::string_view, 3> kMatchEndReasons{"completed", "aborted", "operator"};
inline constexpr std::array<std::string_view, 2> kSocketRoles{"game", "beacon"};

NameList enum_names(Names, Of<ScriptStage>) { return kScriptStages; }
NameList enum_names(Names, Of<LogLevel>) { return kLogLevels; }
NameList enum_names(Names, Of<gc::BuildMatch>) { return kBuildMatches; }
NameList enum_names(Names, Of<gc::PatchStatus>) { return kPatchStatuses; }
NameList enum_names(Names, Of<gc::ExitKind>) { return kExitKinds; }
NameList enum_names(Names, Of<gs::MatchState>) { return kMatchStates; }
NameList enum_names(Names, Of<gs::MatchEndReason>) { return kMatchEndReasons; }
NameList enum_names(Names, Of<gs::SocketRole>) { return kSocketRoles; }

std::string_view variant_key(Names, Of<ScriptPause>) { return "pause"; }
std::string_view variant_key(Names, Of<ScriptDisconnect>) { return "disconnect"; }
std::string_view variant_key(Names, Of<ScriptStopPonging>) { return "stop_ponging"; }
std::string_view variant_key(Names, Of<contracts::common::Log>) { return "log"; }
std::string_view variant_key(Names, Of<gc::Loaded>) { return "loaded"; }
std::string_view variant_key(Names, Of<gc::PatchResult>) { return "patch_result"; }
std::string_view variant_key(Names, Of<gc::RedirectReady>) { return "redirect_ready"; }
std::string_view variant_key(Names, Of<gc::HookFailed>) { return "hook_failed"; }
std::string_view variant_key(Names, Of<gc::LoggedIn>) { return "logged_in"; }
std::string_view variant_key(Names, Of<gc::WindowCreated>) { return "window_created"; }
std::string_view variant_key(Names, Of<gc::ExitRequested>) { return "exit_requested"; }
std::string_view variant_key(Names, Of<gc::ConsoleReady>) { return "console_ready"; }
std::string_view variant_key(Names, Of<gc::Fatal>) { return "fatal"; }
std::string_view variant_key(Names, Of<gs::StateChanged>) { return "state_changed"; }
std::string_view variant_key(Names, Of<gs::PlayerJoined>) { return "player_joined"; }
std::string_view variant_key(Names, Of<gs::PlayerLeft>) { return "player_left"; }
std::string_view variant_key(Names, Of<gs::PlayerCount>) { return "player_count"; }
std::string_view variant_key(Names, Of<gs::MatchEnded>) { return "match_ended"; }
std::string_view variant_key(Names, Of<gs::Fatal>) { return "fatal"; }

namespace {

constexpr std::string_view kInline = "the script";

template <class Script>
[[nodiscard]] Result<Script> parse_script(std::string_view text, std::string_view shown) {
    const auto bad = [&](std::string reason) {
        return make_diag(kTestingDomain, msg::kBadScript).arg("path", shown).arg("reason", reason).kind(ErrorKind::InvalidInput).fail();
    };
    boost::system::error_code error;
    const json::value root = json::parse(text, error);
    if (error) return bad(error.message());
    Script script{};
    if (auto problem = read_struct(root, script, "script")) return bad(std::move(problem->reason));
    return script;
}

template <class Script>
[[nodiscard]] Result<Script> load_script(const NativePath& file) {
    std::ifstream in(file, std::ios::binary);
    if (!in) return make_diag(kTestingDomain, msg::kNotFound).arg("path", file).kind(ErrorKind::NotFound).fail();
    const std::string text{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
    return parse_script<Script>(text, display_utf8(file));
}

template <class Script>
[[nodiscard]] std::string serialize_script(const Script& script) {
    return json::serialize(write_struct(script));
}

}  // namespace
}  // namespace reboot::testing::script_json

namespace reboot::testing {

namespace gc = contracts::game_client;
namespace gs = contracts::game_server;

std::vector<ClientDllStep> default_client_dll_steps() {
    return {gc::Loaded{"fake", 1, gc::BuildMatch::Exact}, gc::RedirectReady{}, gc::LoggedIn{}};
}

gs::GameServerDescription default_fake_description() {
    gs::GameServerDescription description;
    description.protocol = gs::kGameServerProtocol;
    description.build = "fake";
    // The widest range GameVersion::parse accepts.
    description.supports = {gs::VersionSupport{"0.0", "4095.1023", {}}};
    description.sockets = {gs::SocketSpec{gs::SocketRole::Game}};
    description.capabilities = gs::ServerCapabilities{true, false, {}};
    return description;
}

Result<FakeBackendScript> parse_fake_backend_script(std::string_view json) {
    return script_json::parse_script<FakeBackendScript>(json, script_json::kInline);
}

Result<FakeBackendScript> load_fake_backend_script(const NativePath& file) {
    return script_json::load_script<FakeBackendScript>(file);
}

std::string to_json(const FakeBackendScript& script) { return script_json::serialize_script(script); }

Result<FakeGameServerScript> parse_fake_game_server_script(std::string_view json) {
    return script_json::parse_script<FakeGameServerScript>(json, script_json::kInline);
}

Result<FakeGameServerScript> load_fake_game_server_script(const NativePath& file) {
    return script_json::load_script<FakeGameServerScript>(file);
}

std::string to_json(const FakeGameServerScript& script) { return script_json::serialize_script(script); }

Result<FakeGameScript> parse_fake_game_script(std::string_view json) {
    return script_json::parse_script<FakeGameScript>(json, script_json::kInline);
}

Result<FakeGameScript> load_fake_game_script(const NativePath& file) { return script_json::load_script<FakeGameScript>(file); }

std::string to_json(const FakeGameScript& script) { return script_json::serialize_script(script); }

}  // namespace reboot::testing
