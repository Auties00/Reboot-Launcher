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

using reboot::NativePath;
using reboot::os_linux::runner::LinuxRunnerPlatform;
using reboot::os_linux::runner::UmuInvocation;
using reboot::ports::RunnerKind;

using EnvVars = std::vector<std::pair<std::string, std::string>>;

namespace {

struct Fixture {
    reboot::ManualClock clock;
    reboot::ManualExecutor io{clock};
    reboot::testing::ScriptedProcessLauncher processes{io, clock, reboot::testing::FakeOs::Linux};
    LinuxRunnerPlatform platform{processes, reboot::ports::EnvBlock{{{"HOME", "/home/u"}}}, NativePath{"/data/umu"}};
};

reboot::ports::RuntimeLayout umu_layout() {
    return UmuInvocation{NativePath{"/rt/umu/umu-run"}, NativePath{"/rt/ge-proton"}, NativePath{"/data/umu"}}
        .to_runtime_layout();
}

reboot::ports::RuntimeLayout wine_layout() {
    reboot::ports::RuntimeLayout layout;
    layout.root = NativePath{"/rt"};
    layout.entry = NativePath{"/rt/bin/wine"};
    layout.env = {{"WINEDLLOVERRIDES", "winemenubuilder.exe=d"}};
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
    CHECK(layout.error().domain == reboot::ErrorDomain::Internal);
}

TEST_CASE("native and macOS runners are unsupported", "[linux_runner_platform]") {
    Fixture fixture;
    for (const RunnerKind kind : {RunnerKind::Native, RunnerKind::MacRuntime}) {
        const auto layout = fixture.platform.layout(kind, {NativePath{"/rt"}});
        REQUIRE_FALSE(layout);
        CHECK(layout.error().is(reboot::os_linux::runner::kRunnerKindUnsupported));
        CHECK(layout.error().kind == reboot::ErrorKind::Unsupported);
    }
}

TEST_CASE("a Wine launch keeps base and sets only the launcher prefix", "[linux_runner_platform]") {
    Fixture fixture;
    reboot::ports::EnvBlock base{{{"WINEDLLOVERRIDES", "winemenubuilder.exe=d"}, {"WINEPREFIX", "/stale"}}};
    const auto launch = fixture.platform.runner_launch(wine_layout(), NativePath{"/data/prefix"},
                                                       NativePath{"/data/winhost/reboot-winhost.exe"}, base);
    REQUIRE(launch);
    CHECK(launch->exe == NativePath{"/rt/bin/wine"});
    CHECK(launch->args == std::vector<std::string>{"/data/winhost/reboot-winhost.exe"});
    CHECK(launch->cwd == NativePath{"/data/winhost"});
    CHECK(launch->env.vars == EnvVars{{"WINEDLLOVERRIDES", "winemenubuilder.exe=d"}, {"WINEPREFIX", "/data/prefix"}});
    CHECK(launch->stdio == reboot::ports::StdioMode::Capture);
    CHECK(launch->own_group);
}

TEST_CASE("an umu launch also exposes winhost's directory", "[linux_runner_platform]") {
    Fixture fixture;
    reboot::ports::EnvBlock base{{{"PROTONPATH", "/rt/ge-proton"}, {"PRESSURE_VESSEL_FILESYSTEMS_RW", "/games/build"}}};
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
    CHECK(launch.error().is(reboot::os_linux::runner::kPathNotExposable));
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
