#include <catch2/catch_test_macros.hpp>

#include <string>
#include <utility>
#include <vector>

#include "reboot/foundation/secret.hpp"
#include "reboot/play/display_context.hpp"
#include "reboot/play/launch_plan.hpp"
#include "reboot/play/match_targets.hpp"
#include "reboot/process/env_builder.hpp"
#include "reboot/testing/fake_platform_paths.hpp"

using namespace reboot;
using namespace reboot::play;

namespace {

builds::BuildLayout layout(bool companions) {
    builds::BuildLayout out;
    out.root = testing::default_fake_root() / "builds" / "12.41";
    out.shipping_exe = NativePath("FortniteGame/Binaries/Win64") / std::string(builds::kShippingExe);
    if (companions) {
        out.launcher_exe = NativePath("FortniteGame/Binaries/Win64") / std::string(builds::kLauncherExe);
        out.eac_exe = NativePath("FortniteGame/Binaries/Win64") / std::string(builds::kEacExe);
    }
    out.aftermath_dlls = {NativePath("Engine/Binaries/ThirdParty/NVIDIA") / std::string(builds::kAftermathDll),
                          NativePath("FortniteGame/Binaries/Win64") / std::string(builds::kAftermathDll)};
    return out;
}

contracts::ipc::CallerContext caller(std::string session, std::vector<contracts::ipc::EnvVar> env = {}) {
    return contracts::ipc::CallerContext{std::move(session), false, std::move(env)};
}

}  // namespace

TEST_CASE("companions are the launcher, then the EAC exe, each only when present", "[play][plan]") {
    const builds::BuildLayout full = layout(true);
    const std::vector<ports::CompanionSpec> companions = companions_for(full);
    REQUIRE(companions.size() == 2);
    CHECK(companions[0].exe == full.root / *full.launcher_exe);
    CHECK(companions[1].exe == full.root / *full.eac_exe);
    CHECK(companions[0].args.empty());
    CHECK(companions_for(layout(false)).empty());

    builds::BuildLayout eac_only = layout(false);
    eac_only.eac_exe = NativePath(std::string(builds::kEacExe));
    REQUIRE(companions_for(eac_only).size() == 1);
    CHECK(companions_for(eac_only)[0].exe == eac_only.root / std::string(builds::kEacExe));
}

TEST_CASE("every Aftermath DLL is parked, as an absolute path", "[play][plan]") {
    const builds::BuildLayout full = layout(false);
    const std::vector<NativePath> parked = parked_for(full);
    REQUIRE(parked.size() == 2);
    CHECK(parked[0] == full.root / full.aftermath_dlls[0]);
    CHECK(parked[1] == full.root / full.aftermath_dlls[1]);
    CHECK(parked_for(builds::BuildLayout{}).empty());
}

TEST_CASE("a session launch copies the plan with its argv and environment", "[play][plan]") {
    LaunchPlan plan;
    const builds::BuildLayout full = layout(true);
    plan.exe = full.root / full.shipping_exe;
    plan.cwd = full.binaries_dir();
    identity::LoginPlan login;
    login.auth_login = "a@projectreboot.dev";
    Result<LaunchArgs> args = build_launch_args(login, SecretString{std::string("credential")}, "system", {});
    REQUIRE(args);
    plan.args = std::move(*args);
    process::EnvBuilder builder(process::EnvSyntax::Windows);
    builder.channel("REBOOT_ROLE", std::string(kClientRole));
    Result<process::BuiltEnv> env = std::move(builder).build();
    REQUIRE(env);
    plan.env = std::move(*env);
    plan.companions = companions_for(full);
    plan.park = parked_for(full);
    plan.inject = {ports::InjectEntry{full.root / "rb_client.dll", {}, ports::BootStrategy::EarlyBirdApc, ports::InjectPhase::Early}};
    plan.multiplier = RunnerMultiplier::Wine;

    Uuid uuid{};
    uuid.bytes[0] = 7;
    const SessionId session{uuid};
    const ports::SessionLaunch launch = plan.session_launch(session);
    CHECK(launch.session == session);
    CHECK(launch.exe == plan.exe);
    CHECK(launch.cwd == plan.cwd);
    CHECK(launch.args == plan.args.argv());
    REQUIRE(launch.env.vars.size() == 1);
    CHECK(launch.env.vars[0] == std::pair<std::string, std::string>{"REBOOT_ROLE", "client"});
    CHECK(launch.companions.size() == 2);
    CHECK(launch.inject.size() == 1);
    CHECK(launch.park == plan.park);
    CHECK(launch.multiplier == RunnerMultiplier::Wine);
}

TEST_CASE("play needs the engine's OS session", "[play][display]") {
    CHECK(check_display(caller("3"), "3", SessionMatch::OsSession));
    const Result<void> wrong = check_display(caller("2"), "3", SessionMatch::OsSession);
    REQUIRE_FALSE(wrong);
    CHECK(wrong.error().id == "play.wrong_session");
}

TEST_CASE("on Linux a usable DISPLAY or WAYLAND_DISPLAY is what counts", "[play][display]") {
    CHECK(check_display(caller("", {{"DISPLAY", ":0"}}), "1", SessionMatch::Display));
    CHECK(check_display(caller("", {{"WAYLAND_DISPLAY", "wayland-0"}}), "1", SessionMatch::Display));
    for (const auto& env : {std::vector<contracts::ipc::EnvVar>{}, std::vector<contracts::ipc::EnvVar>{{"DISPLAY", ""}},
                            std::vector<contracts::ipc::EnvVar>{{"XAUTHORITY", "/x"}}}) {
        const Result<void> none = check_display(caller("1", env), "1", SessionMatch::Display);
        REQUIRE_FALSE(none);
        CHECK(none.error().id == "play.no_display");
    }
}

TEST_CASE("match targets hold one entry and answer for its account only", "[play][match]") {
    MatchTargets targets;
    Uuid a{};
    a.bytes[0] = 1;
    Uuid b{};
    b.bytes[0] = 2;
    const SessionId first{a};
    const SessionId second{b};
    CHECK_FALSE(targets.resolve({"Player-abc", "playlist_defaultsolo"}).endpoint);

    targets.publish(first, MatchTargetEntry{"Player-abc", HostPort{"1.2.3.4", Port{7777}}, Port{15000}});
    const backend::ResolvedMatchTarget resolved = targets.resolve({"Player-abc", "any"});
    CHECK(resolved.endpoint == HostPort{"1.2.3.4", Port{7777}});
    CHECK(resolved.beacon_port == Port{15000});
    CHECK_FALSE(targets.resolve({"Someone-else", "any"}).endpoint);
    CHECK(targets.find(first));
    CHECK_FALSE(targets.find(second));

    // Another session's withdraw leaves the entry; a publish replaces it.
    targets.withdraw(second);
    CHECK(targets.find(first));
    targets.publish(second, MatchTargetEntry{"Player-abc", HostPort{"5.6.7.8", Port{7778}}, std::nullopt});
    CHECK_FALSE(targets.find(first));
    CHECK(targets.resolve({"Player-abc", ""}).endpoint == HostPort{"5.6.7.8", Port{7778}});
    targets.withdraw(first);
    CHECK(targets.find(second));
    targets.withdraw(second);
    CHECK_FALSE(targets.resolve({"Player-abc", ""}).endpoint);
}
