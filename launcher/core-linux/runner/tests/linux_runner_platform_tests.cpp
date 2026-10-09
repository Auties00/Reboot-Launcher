#include <catch2/catch_test_macros.hpp>

#include <string>
#include <utility>
#include <vector>

#include "messages.hpp"
#include "reboot/foundation/clock.hpp"
#include "reboot/foundation/executor.hpp"
#include "reboot/os_linux/runner/linux_runner_platform.hpp"
#include "reboot/os_linux/runner/umu_invocation.hpp"
#include "reboot/testing/fake_os.hpp"
#include "reboot/testing/scripted_process_launcher.hpp"

using rb::NativePath;
using rb::os_linux::runner::LinuxRunnerPlatform;
using rb::os_linux::runner::UmuInvocation;
using rb::ports::RunnerKind;

using EnvVars = std::vector<std::pair<std::string, std::string>>;

namespace {

struct Fixture {
    rb::ManualClock clock;
    rb::ManualExecutor io{clock};
    rb::testing::ScriptedProcessLauncher processes{io, clock, rb::testing::FakeOs::Linux};
    LinuxRunnerPlatform platform{processes, rb::ports::EnvBlock{{{"HOME", "/home/u"}}}, NativePath{"/data/umu"}};
};

rb::ports::RuntimeLayout umu_layout() {
    return UmuInvocation{NativePath{"/rt/umu/umu-run"}, NativePath{"/rt/ge-proton"}, NativePath{"/data/umu"}}
        .to_runtime_layout();
}

rb::ports::RuntimeLayout wine_layout() {
    rb::ports::RuntimeLayout layout;
    layout.root = NativePath{"/rt"};
    layout.entry = NativePath{"/rt/bin/wine"};
    return layout;
}

}  // namespace

TEST_CASE("umu plays first and Wine stays available", "[linux_runner_platform]") {
    Fixture fixture;
    CHECK(fixture.platform.supported() == std::vector{RunnerKind::Umu, RunnerKind::Wine});
}

TEST_CASE("an umu layout without the umu launcher is a bug", "[linux_runner_platform]") {
    Fixture fixture;
    const auto layout = fixture.platform.layout(RunnerKind::Umu, {NativePath{"/rt/ge-proton"}});
    REQUIRE_FALSE(layout);
    CHECK(layout.error().domain == rb::ErrorDomain::Internal);
}

TEST_CASE("native and macOS runners are unsupported", "[linux_runner_platform]") {
    Fixture fixture;
    for (const RunnerKind kind : {RunnerKind::Native, RunnerKind::MacRuntime}) {
        const auto layout = fixture.platform.layout(kind, {NativePath{"/rt"}});
        REQUIRE_FALSE(layout);
        CHECK(layout.error().is(rb::os_linux::runner::kRunnerKindUnsupported));
        CHECK(layout.error().kind == rb::ErrorKind::Unsupported);
    }
}

TEST_CASE("a Wine launch keeps base and sets only the launcher prefix", "[linux_runner_platform]") {
    Fixture fixture;
    rb::ports::EnvBlock base{{{"WINEDLLOVERRIDES", "winemenubuilder.exe=d"}, {"WINEPREFIX", "/stale"}}};
    const auto launch = fixture.platform.runner_launch(wine_layout(), NativePath{"/data/prefix"},
                                                       NativePath{"/data/winhost/reboot-winhost.exe"}, base);
    REQUIRE(launch);
    CHECK(launch->exe == NativePath{"/rt/bin/wine"});
    CHECK(launch->args == std::vector<std::string>{"/data/winhost/reboot-winhost.exe"});
    CHECK(launch->cwd == NativePath{"/data/winhost"});
    CHECK(launch->env.vars == EnvVars{{"WINEDLLOVERRIDES", "winemenubuilder.exe=d"}, {"WINEPREFIX", "/data/prefix"}});
    CHECK(launch->stdio == rb::ports::StdioMode::Capture);
    CHECK(launch->own_group);
}

TEST_CASE("an umu launch also exposes winhost's directory", "[linux_runner_platform]") {
    Fixture fixture;
    rb::ports::EnvBlock base{{{"PROTONPATH", "/rt/ge-proton"}, {"PRESSURE_VESSEL_FILESYSTEMS_RW", "/games/build"}}};
    const auto launch = fixture.platform.runner_launch(umu_layout(), NativePath{"/data/prefix"},
                                                       NativePath{"/data/winhost/reboot-winhost.exe"}, base);
    REQUIRE(launch);
    CHECK(launch->exe == NativePath{"/rt/umu/umu-run"});
    CHECK(launch->env.vars == EnvVars{{"PROTONPATH", "/rt/ge-proton"},
                                      {"WINEPREFIX", "/data/prefix"},
                                      {"PRESSURE_VESSEL_FILESYSTEMS_RW", "/games/build:/data/winhost"}});
}

TEST_CASE("an umu launch refuses a winhost directory with a colon", "[linux_runner_platform]") {
    Fixture fixture;
    const auto launch = fixture.platform.runner_launch(umu_layout(), NativePath{"/data/prefix"},
                                                       NativePath{"/data:x/reboot-winhost.exe"}, {});
    REQUIRE_FALSE(launch);
    CHECK(launch.error().is(rb::os_linux::runner::kPathNotExposable));
}

TEST_CASE("a Wine runtime needs no setup", "[linux_runner_platform]") {
    Fixture fixture;
    CHECK(fixture.platform.runtime_setup(wine_layout(), {}));
    CHECK(fixture.processes.children().empty());
}

TEST_CASE("Linux raises no prerequisite request", "[linux_runner_platform]") {
    Fixture fixture;
    CHECK_FALSE(fixture.platform.pending_prerequisite());
}

TEST_CASE("a Wine prefix boots through wineboot and stops through its wineserver", "[linux_runner_platform]") {
    Fixture fixture;
    const auto boot = fixture.platform.prefix_command(wine_layout(), NativePath{"/data/prefixes/wine"},
                                                      {rb::ports::PrefixVerb::Boot, {}, {}}, {});
    REQUIRE(boot);
    CHECK(boot->exe == NativePath{"/rt/bin/wine"});
    CHECK(boot->args == std::vector<std::string>{"wineboot", "-u"});
    CHECK(boot->cwd == NativePath{"/data/prefixes"});
    CHECK(boot->env.vars == EnvVars{{"WINEPREFIX", "/data/prefixes/wine"}});

    const auto kill = fixture.platform.prefix_command(wine_layout(), NativePath{"/data/prefixes/wine"},
                                                      {rb::ports::PrefixVerb::KillServer, {}, {}}, {});
    REQUIRE(kill);
    CHECK(kill->exe == NativePath{"/rt/bin/wineserver"});
    CHECK(kill->args == std::vector<std::string>{"-k"});
}

TEST_CASE("an umu prefix is built by umu and runs programs from exposed directories", "[linux_runner_platform]") {
    Fixture fixture;
    const auto boot = fixture.platform.prefix_command(umu_layout(), NativePath{"/data/prefixes/umu"},
                                                      {rb::ports::PrefixVerb::Boot, {}, {}}, {});
    REQUIRE(boot);
    CHECK(boot->exe == NativePath{"/rt/umu/umu-run"});
    CHECK(boot->args == std::vector<std::string>{"createprefix"});

    const auto kill = fixture.platform.prefix_command(umu_layout(), NativePath{"/data/prefixes/umu"},
                                                      {rb::ports::PrefixVerb::KillServer, {}, {}}, {});
    REQUIRE(kill);
    CHECK(kill->exe == NativePath{"/rt/ge-proton/files/bin/wineserver"});

    const rb::ports::PrefixCommand run{rb::ports::PrefixVerb::Run, NativePath{"/data/vc/vc_redist.x64.exe"},
                                           {"/install", "/quiet"}};
    const auto launch = fixture.platform.prefix_command(umu_layout(), NativePath{"/data/prefixes/umu"}, run, {});
    REQUIRE(launch);
    CHECK(launch->args == std::vector<std::string>{"/data/vc/vc_redist.x64.exe", "/install", "/quiet"});
    CHECK(launch->cwd == NativePath{"/data/vc"});
    CHECK(launch->env.vars == EnvVars{{"WINEPREFIX", "/data/prefixes/umu"}, {"PRESSURE_VESSEL_FILESYSTEMS_RW", "/data/vc"}});
}

TEST_CASE("a Wine runtime setup reports no build", "[linux_runner_platform]") {
    Fixture fixture;
    const auto build = fixture.platform.runtime_setup(wine_layout(), {});
    REQUIRE(build);
    CHECK_FALSE(build->has_value());
}
