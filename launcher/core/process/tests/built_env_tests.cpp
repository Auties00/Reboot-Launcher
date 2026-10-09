#include <catch2/catch_test_macros.hpp>
#include <algorithm>
#include <string>
#include <utility>
#include <vector>

#include "reboot/contracts/game_client.hpp"
#include "reboot/process/env_builder.hpp"
#include "reboot/process/log_string.hpp"
#include "reboot/process/wiping_launch.hpp"

using namespace reboot;
using namespace reboot::process;

namespace {

BuiltEnv build(EnvBuilder builder) {
    Result<BuiltEnv> env = std::move(builder).build();
    REQUIRE(env.has_value());
    return std::move(*env);
}

BuiltEnv token_env(EnvSyntax syntax) {
    EnvBuilder builder(syntax);
    builder.daemon_base(ports::EnvBlock{{{"PATH", "/bin"}}})
        .channel_secret(contracts::game_client::kEnvCtlToken, SecretString(std::string("s3cret")));
    return build(std::move(builder));
}

}  // namespace

TEST_CASE("a moved BuiltEnv keeps its values and secrets; the source is empty", "[process][env]") {
    BuiltEnv source = token_env(EnvSyntax::Posix);
    BuiltEnv moved = std::move(source);
    CHECK(moved.vars().vars.size() == 2);
    CHECK(moved.sensitive(contracts::game_client::kEnvCtlToken));
    CHECK(source.vars().vars.empty());

    BuiltEnv assigned;
    assigned = std::move(moved);
    CHECK(assigned.vars().vars.size() == 2);
    CHECK(assigned.sensitive(contracts::game_client::kEnvCtlToken));
    CHECK(moved.vars().vars.empty());
}

TEST_CASE("copy gives the launch its own block", "[process][env]") {
    const BuiltEnv env = token_env(EnvSyntax::Posix);
    ports::EnvBlock copy = env.copy();
    REQUIRE(copy.vars == env.vars().vars);
    copy.vars.clear();
    CHECK(env.vars().vars.size() == 2);
}

TEST_CASE("sensitivity follows the syntax's name comparison", "[process][env]") {
    const BuiltEnv windows = token_env(EnvSyntax::Windows);
    CHECK(windows.sensitive("reboot_ctl_token"));
    CHECK_FALSE(windows.sensitive("PATH"));
    const BuiltEnv posix = token_env(EnvSyntax::Posix);
    CHECK_FALSE(posix.sensitive("reboot_ctl_token"));
}

TEST_CASE("denied names are listed for the debug log", "[process][env]") {
    EnvBuilder builder(EnvSyntax::Posix);
    builder.daemon_base(ports::EnvBlock{{{"HOME", "/h"}}})
        .profile(ports::EnvBlock{{{"PROTON_ENABLE_NVAPI", "1"}}})
        .runner(ports::EnvBlock{{{"LD_LIBRARY_PATH", "/x"}, {"STEAM_COMPAT_DATA_PATH", "/p"}, {"WINEPREFIX", "/w"}}});
    const BuiltEnv env = build(std::move(builder));
    std::vector<std::string> denied = env.denied();
    std::ranges::sort(denied);
    CHECK(denied == std::vector<std::string>{"LD_LIBRARY_PATH", "STEAM_COMPAT_DATA_PATH"});
    CHECK(to_log_string(env).find("WINEPREFIX=/w\n") != std::string::npos);
}

TEST_CASE("the Windows block encodes non-ASCII as UTF-16LE with surrogates", "[process][env]") {
    EnvBuilder builder(EnvSyntax::Windows);
    // U+00E9 and U+1F600.
    builder.daemon_base(ports::EnvBlock{{{"N", "\xC3\xA9\xF0\x9F\x98\x80"}}});
    const BuiltEnv env = build(std::move(builder));
    CHECK(env.windows_block().reveal() ==
          std::vector<u8>{'N', 0, '=', 0, 0xE9, 0x00, 0x3D, 0xD8, 0x00, 0xDE, 0, 0, 0, 0});
}

TEST_CASE("the Windows block of a POSIX environment is in case-insensitive order", "[process][env]") {
    EnvBuilder builder(EnvSyntax::Posix);
    builder.runner(ports::EnvBlock{{{"zeta", "1"}, {"_X", "2"}, {"Alpha", "3"}}});
    const BuiltEnv env = build(std::move(builder));
    const SecretBytes block = env.windows_block();
    const std::vector<u8>& bytes = block.reveal();
    std::vector<std::string> names;
    std::string entry;
    for (std::size_t i = 0; i + 1 < bytes.size(); i += 2) {
        const auto unit = static_cast<char16_t>(bytes[i] | (bytes[i + 1] << 8));
        if (unit != 0) {
            entry.push_back(static_cast<char>(unit));
            continue;
        }
        if (entry.empty()) break;
        names.push_back(entry.substr(0, entry.find('=')));
        entry.clear();
    }
    CHECK(names == std::vector<std::string>{"Alpha", "WINEDEBUG", "zeta", "_X"});
}

TEST_CASE("a native launch refuses a channel name outside REBOOT_", "[process][env]") {
    EnvBuilder builder(EnvSyntax::Posix);
    builder.channel("OTHER", "1");
    Result<BuiltEnv> env = std::move(builder).build();
    REQUIRE_FALSE(env.has_value());
    CHECK(env.error().id == "process.env_channel_name");
}

TEST_CASE("an invalid client value fails the build", "[process][env]") {
    contracts::ipc::CallerContext caller;
    caller.display_env.push_back({"DISPLAY", std::string(":0\0x", 4)});
    EnvBuilder builder(EnvSyntax::Posix);
    builder.client(caller);
    Result<BuiltEnv> env = std::move(builder).build();
    REQUIRE_FALSE(env.has_value());
    CHECK(env.error().id == "process.env_invalid_value");
}

TEST_CASE("a WipingLaunch moves its launch and leaves the source without an environment", "[process][spec]") {
    ports::ProcessLaunch launch;
    launch.exe = NativePath("game");
    launch.env.vars.emplace_back("REBOOT_CTL_TOKEN", "secret");
    WipingLaunch first(std::move(launch));
    WipingLaunch second(std::move(first));
    CHECK(second.get().env.vars.size() == 1);
    CHECK(first.get().env.vars.empty());

    ports::ProcessLaunch other;
    other.env.vars.emplace_back("A", "1");
    WipingLaunch third{std::move(other)};
    third = std::move(second);
    REQUIRE(third.get().env.vars.size() == 1);
    CHECK(third.get().env.vars[0].second == "secret");
    CHECK(second.get().env.vars.empty());
}
