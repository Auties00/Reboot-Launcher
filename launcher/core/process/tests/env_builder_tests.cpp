#include <catch2/catch_test_macros.hpp>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "reboot/contracts/game_client.hpp"
#include "reboot/process/env_builder.hpp"
#include "reboot/process/log_string.hpp"
#include "reboot/storage/settings_values.hpp"

using namespace rb;
using namespace rb::process;

namespace {

ports::EnvBlock block(std::vector<std::pair<std::string, std::string>> vars) { return ports::EnvBlock{std::move(vars)}; }

std::optional<std::string> value_of(const BuiltEnv& env, std::string_view name) {
    for (const auto& [n, v] : env.vars().vars)
        if (n == name) return v;
    return std::nullopt;
}

bool has(const BuiltEnv& env, std::string_view name) { return value_of(env, name).has_value(); }

BuiltEnv build(EnvBuilder builder) {
    Result<BuiltEnv> env = std::move(builder).build();
    REQUIRE(env.has_value());
    return std::move(*env);
}

}  // namespace

TEST_CASE("the POSIX base keeps only the daemon base names", "[process][env]") {
    EnvBuilder builder(EnvSyntax::Posix);
    builder.daemon_base(block({{"HOME", "/home/u"},
                               {"SHELL", "/bin/sh"},
                               {"XDG_CACHE_HOME", "/c"},
                               {"XDG_STATE_HOME", "/s"},
                               {"XDG_DATA_DIRS", "/d"},
                               {"EDITOR", "vi"}}));
    const BuiltEnv env = build(std::move(builder));
    CHECK(value_of(env, "HOME") == "/home/u");
    CHECK(has(env, "SHELL"));
    CHECK(has(env, "XDG_CACHE_HOME"));
    CHECK_FALSE(has(env, "XDG_STATE_HOME"));
    CHECK_FALSE(has(env, "XDG_DATA_DIRS"));
    CHECK_FALSE(has(env, "EDITOR"));
}

TEST_CASE("the Windows base is taken whole except invalid names", "[process][env]") {
    EnvBuilder builder(EnvSyntax::Windows);
    builder.daemon_base(block({{"USERPROFILE", "C:\\Users\\u"}, {"=C:", "C:\\"}, {"TEMP", "C:\\t"}}));
    const BuiltEnv env = build(std::move(builder));
    CHECK(has(env, "USERPROFILE"));
    CHECK(has(env, "TEMP"));
    CHECK_FALSE(has(env, "=C:"));
}

TEST_CASE("client and profile layers pass only their lists, and later layers win", "[process][env]") {
    EnvBuilder builder(EnvSyntax::Posix);
    contracts::ipc::CallerContext caller;
    caller.display_env = {{"DISPLAY", ":0"}, {"LC_ALL", "C"}, {"SSH_AUTH_SOCK", "/s"}};
    builder.daemon_base(block({{"PATH", "/usr/bin"}}))
        .client(caller)
        .profile(block({{"DXVK_HUD", "1"}, {"DISPLAY", ":9"}, {"PATH", "/evil"}}))
        .runner(block({{"PATH", "/runtime/bin"}}));
    const BuiltEnv env = build(std::move(builder));
    CHECK(value_of(env, "DISPLAY") == ":0");
    CHECK(has(env, "LC_ALL"));
    CHECK_FALSE(has(env, "SSH_AUTH_SOCK"));
    CHECK(value_of(env, "DXVK_HUD") == "1");
    CHECK(value_of(env, "PATH") == "/runtime/bin");
}

TEST_CASE("the deny-list runs last, with runner exemptions", "[process][env]") {
    EnvBuilder builder(EnvSyntax::Posix);
    contracts::ipc::CallerContext caller;
    caller.display_env = {{"LANG", "C"}};
    builder.client(caller)
        .profile(block({{"PROTON_ENABLE_NVAPI", "1"}}))
        .runner(block({{"WINEPREFIX", "/prefix"}, {"LD_PRELOAD", "x.so"}, {"SteamGameId", "1"}}));
    const BuiltEnv env = build(std::move(builder));
    CHECK(value_of(env, "WINEPREFIX") == "/prefix");
    CHECK_FALSE(has(env, "LD_PRELOAD"));
    CHECK_FALSE(has(env, "SteamGameId"));
    CHECK(has(env, "PROTON_ENABLE_NVAPI"));
    CHECK(env.denied() == std::vector<std::string>{"LD_PRELOAD", "SteamGameId"});
}

TEST_CASE("a profile cannot set WINEPREFIX", "[process][env]") {
    EnvBuilder builder(EnvSyntax::Posix);
    builder.daemon_base(block({{"PATH", "/bin"}})).profile(block({{"WINEPREFIX", "/user"}})).runner(block({}));
    CHECK_FALSE(has(build(std::move(builder)), "WINEPREFIX"));
}

TEST_CASE("a Wine launch carries only the named REBOOT_ variables", "[process][env]") {
    EnvBuilder ok(EnvSyntax::Posix);
    ok.runner(block({{"REBOOT_STRAY", "1"}}))
        .channel(contracts::game_client::kEnvCtl, "tcp://127.0.0.1:1")
        .channel(contracts::game_client::kEnvSession, "s");
    const BuiltEnv env = build(std::move(ok));
    CHECK(has(env, contracts::game_client::kEnvCtl));
    CHECK(has(env, contracts::game_client::kEnvSession));
    CHECK_FALSE(has(env, "REBOOT_STRAY"));

    EnvBuilder bad(EnvSyntax::Posix);
    bad.runner(block({})).channel("REBOOT_ENDPOINT", "x");
    Result<BuiltEnv> rejected = std::move(bad).build();
    REQUIRE_FALSE(rejected.has_value());
    CHECK(rejected.error().id == "process.env_channel_name");
}

TEST_CASE("a native launch takes REBOOT_ channel names and strips stray ones", "[process][env]") {
    EnvBuilder builder(EnvSyntax::Windows);
    builder.daemon_base(block({{"reboot_password", "hunter2"}})).channel("REBOOT_BACKEND_DATA", "C:\\data");
    const BuiltEnv env = build(std::move(builder));
    CHECK(has(env, "REBOOT_BACKEND_DATA"));
    CHECK_FALSE(has(env, "reboot_password"));

    EnvBuilder bad(EnvSyntax::Posix);
    bad.channel("PATH", "/x");
    CHECK_FALSE(std::move(bad).build().has_value());
}

TEST_CASE("WINEDEBUG is set on runner launches only, from the profile or the default", "[process][env]") {
    EnvBuilder fallback(EnvSyntax::Posix);
    fallback.daemon_base(block({{"PATH", "/bin"}})).runner(block({{"WINEDEBUG", "+all"}}));
    CHECK(value_of(build(std::move(fallback)), kWineDebugName) == std::string(kWineDebugDefault));

    EnvBuilder profiled(EnvSyntax::Posix);
    profiled.profile(block({{"WINEDEBUG", "+seh"}})).runner(block({}));
    CHECK(value_of(build(std::move(profiled)), kWineDebugName) == "+seh");

    EnvBuilder native(EnvSyntax::Posix);
    native.profile(block({{"WINEDEBUG", "+seh"}}));
    CHECK_FALSE(has(build(std::move(native)), kWineDebugName));
}

TEST_CASE("OPENSSL_ia32cap is only ever the opt-in value", "[process][env]") {
    EnvBuilder off(EnvSyntax::Windows);
    off.daemon_base(block({{"OPENSSL_ia32cap", "~0x0"}}));
    CHECK_FALSE(has(build(std::move(off)), kOpensslIa32capName));

    EnvBuilder on(EnvSyntax::Windows);
    on.daemon_base(block({{"OPENSSL_ia32cap", "~0x0"}})).openssl_ia32cap(true);
    CHECK(value_of(build(std::move(on)), kOpensslIa32capName) == std::string(kOpensslIa32capValue));
}

TEST_CASE("PROTON_LOG comes only from the opt-in", "[process][env]") {
    EnvBuilder inherited(EnvSyntax::Posix);
    inherited.profile(block({{"PROTON_LOG", "1"}, {"PROTON_LOG_DIR", "/tmp"}})).runner(block({}));
    const BuiltEnv quiet = build(std::move(inherited));
    CHECK_FALSE(has(quiet, kProtonLogName));
    CHECK_FALSE(has(quiet, kProtonLogDirName));

    EnvBuilder verbose(EnvSyntax::Posix);
    verbose.runner(block({})).proton_log(NativePath("/logs"));
    const BuiltEnv logged = build(std::move(verbose));
    CHECK(value_of(logged, kProtonLogName) == "1");
    CHECK(value_of(logged, kProtonLogDirName) == "/logs");
}

TEST_CASE("an invalid value outside the base fails the build", "[process][env]") {
    EnvBuilder builder(EnvSyntax::Posix);
    builder.profile(block({{"DRI_PRIME", std::string("1\0", 2)}}));
    Result<BuiltEnv> env = std::move(builder).build();
    REQUIRE_FALSE(env.has_value());
    CHECK(env.error().id == "process.env_invalid_value");
}

TEST_CASE("secret channel values are masked in the log string", "[process][env]") {
    EnvBuilder builder(EnvSyntax::Posix);
    builder.channel(contracts::game_client::kEnvCtl, "tcp://127.0.0.1:1")
        .channel_secret(contracts::game_client::kEnvCtlToken, SecretString(std::string("token-value")));
    const BuiltEnv env = build(std::move(builder));
    CHECK(env.sensitive(contracts::game_client::kEnvCtlToken));
    CHECK(to_log_string(env) == "REBOOT_CTL=tcp://127.0.0.1:1\nREBOOT_CTL_TOKEN=***\n");
}

TEST_CASE("a fork carries the layers so far and diverges after", "[process][env]") {
    EnvBuilder base(EnvSyntax::Posix);
    base.daemon_base(block({{"HOME", "/h"}})).runner(block({}));
    EnvBuilder game = base.fork();
    base.channel(contracts::game_client::kEnvCtlToken, "winhost");
    game.channel(contracts::game_client::kEnvCtlToken, "game");
    const BuiltEnv winhost_env = build(std::move(base));
    const BuiltEnv game_env = build(std::move(game));
    CHECK(value_of(winhost_env, contracts::game_client::kEnvCtlToken) == "winhost");
    CHECK(value_of(game_env, contracts::game_client::kEnvCtlToken) == "game");
    CHECK(value_of(game_env, "HOME") == "/h");
}

TEST_CASE("Windows names compare case-insensitively and sort that way", "[process][env]") {
    EnvBuilder builder(EnvSyntax::Windows);
    builder.daemon_base(block({{"Path", "C:\\a"}, {"b", "1"}, {"dxvk_hud", "0"}, {"A", "2"}}))
        .profile(block({{"DXVK_HUD", "1"}}))
        .channel("REBOOT_X", "1");
    const BuiltEnv env = build(std::move(builder));
    std::vector<std::string> names;
    for (const auto& [name, value] : env.vars().vars) names.push_back(name);
    CHECK(names == std::vector<std::string>{"A", "b", "DXVK_HUD", "Path", "REBOOT_X"});
    CHECK(value_of(env, "DXVK_HUD") == "1");
}

TEST_CASE("the Windows block is UTF-16LE entries and a double NUL", "[process][env]") {
    EnvBuilder builder(EnvSyntax::Windows);
    builder.daemon_base(block({{"A", "1"}}));
    const BuiltEnv env = build(std::move(builder));
    const SecretBytes bytes = env.windows_block();
    CHECK(bytes.reveal() == std::vector<u8>{'A', 0, '=', 0, '1', 0, 0, 0, 0, 0});

    const BuiltEnv empty = build(EnvBuilder(EnvSyntax::Windows));
    CHECK(empty.windows_block().reveal() == std::vector<u8>{0, 0, 0, 0});
}

TEST_CASE("play settings feed the pass-through layer and the Proton log", "[process][env]") {
    storage::PlaySettings play;
    play.env = "DRI_PRIME=1\nLD_PRELOAD=x.so\nWINEDEBUG=+seh";
    play.verbose_wine_log = true;

    EnvBuilder wine(EnvSyntax::Posix);
    wine.play_settings(play, NativePath("/logs/wine")).runner(block({}));
    const BuiltEnv wine_env = build(std::move(wine));
    CHECK(value_of(wine_env, "DRI_PRIME") == "1");
    CHECK_FALSE(has(wine_env, "LD_PRELOAD"));
    CHECK(value_of(wine_env, kWineDebugName) == "+seh");
    CHECK(value_of(wine_env, kProtonLogDirName) == "/logs/wine");

    EnvBuilder native(EnvSyntax::Windows);
    native.play_settings(play, NativePath());
    const BuiltEnv native_env = build(std::move(native));
    CHECK(value_of(native_env, "DRI_PRIME") == "1");
    CHECK_FALSE(has(native_env, kProtonLogName));

    play.verbose_wine_log = false;
    EnvBuilder quiet(EnvSyntax::Posix);
    quiet.play_settings(play, NativePath("/logs/wine")).runner(block({}));
    CHECK_FALSE(has(build(std::move(quiet)), kProtonLogName));
}
