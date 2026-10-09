#include <catch2/catch_test_macros.hpp>

#include <string>
#include <utility>
#include <vector>

#include "messages.hpp"
#include "reboot/os_macos/runner/dxmt_wine_runner_platform.hpp"

using reboot::ErrorKind;
using reboot::NativePath;
using reboot::os_macos::runner::DxmtWineRunnerPlatform;
using reboot::os_macos::runner::HostCpu;
using reboot::ports::RunnerKind;

using EnvVars = std::vector<std::pair<std::string, std::string>>;

TEST_CASE("only Apple Silicon supports the macOS runtime", "[dxmt_wine_runner_platform]") {
    CHECK(DxmtWineRunnerPlatform{HostCpu::AppleSilicon}.supported() == std::vector{RunnerKind::MacRuntime});
    CHECK(DxmtWineRunnerPlatform{HostCpu::Intel}.supported().empty());
}

TEST_CASE("an Intel Mac cannot lay out the runtime", "[dxmt_wine_runner_platform]") {
    DxmtWineRunnerPlatform platform{HostCpu::Intel};
    const auto layout = platform.layout(RunnerKind::MacRuntime, {NativePath{"/rt"}});
    REQUIRE_FALSE(layout);
    CHECK(layout.error().is(reboot::os_macos::runner::kNeedsAppleSilicon));
    CHECK(layout.error().kind == ErrorKind::Unsupported);
}

TEST_CASE("every other runner kind is unsupported", "[dxmt_wine_runner_platform]") {
    DxmtWineRunnerPlatform platform{HostCpu::AppleSilicon};
    for (const RunnerKind kind : {RunnerKind::Native, RunnerKind::Umu, RunnerKind::Wine}) {
        const auto layout = platform.layout(kind, {NativePath{"/rt"}});
        REQUIRE_FALSE(layout);
        CHECK(layout.error().is(reboot::os_macos::runner::kRunnerKindUnsupported));
    }
}

TEST_CASE("an Intel Mac never asks for Rosetta", "[dxmt_wine_runner_platform]") {
    CHECK_FALSE(DxmtWineRunnerPlatform{HostCpu::Intel}.pending_prerequisite());
}

TEST_CASE("the launch keeps base and sets only the launcher prefix", "[dxmt_wine_runner_platform]") {
    DxmtWineRunnerPlatform platform{HostCpu::AppleSilicon};
    reboot::ports::RuntimeLayout layout;
    layout.root = NativePath{"/rt"};
    layout.entry = NativePath{"/rt/bin/wine"};
    layout.env = {{"WINEDLLOVERRIDES", "not-reapplied"}};
    reboot::ports::EnvBlock base{{{"WINEDLLOVERRIDES", "winemenubuilder.exe=d"}, {"WINEPREFIX", "/stale"}}};

    const auto launch =
        platform.runner_launch(layout, NativePath{"/data/prefix"}, NativePath{"/data/winhost/reboot-winhost.exe"}, base);
    REQUIRE(launch);
    CHECK(launch->exe == NativePath{"/rt/bin/wine"});
    CHECK(launch->args == std::vector<std::string>{"/data/winhost/reboot-winhost.exe"});
    CHECK(launch->cwd == NativePath{"/data/winhost"});
    CHECK(launch->env.vars == EnvVars{{"WINEDLLOVERRIDES", "winemenubuilder.exe=d"}, {"WINEPREFIX", "/data/prefix"}});
    CHECK(launch->stdio == reboot::ports::StdioMode::Capture);
    CHECK(launch->own_group);
}

TEST_CASE("an empty base gains only the prefix", "[dxmt_wine_runner_platform]") {
    DxmtWineRunnerPlatform platform{HostCpu::AppleSilicon};
    reboot::ports::RuntimeLayout layout;
    layout.root = NativePath{"/rt"};
    layout.entry = NativePath{"/rt/bin/wine"};

    const auto launch = platform.runner_launch(layout, NativePath{"/data/prefix"}, NativePath{"/w/reboot-winhost.exe"}, {});
    REQUIRE(launch);
    CHECK(launch->env.vars == EnvVars{{"WINEPREFIX", "/data/prefix"}});
}

TEST_CASE("the macOS runtime needs no setup", "[dxmt_wine_runner_platform]") {
    DxmtWineRunnerPlatform platform{HostCpu::AppleSilicon};
    reboot::ports::RuntimeLayout layout;
    layout.root = NativePath{"/rt"};
    layout.entry = NativePath{"/rt/bin/wine"};
    CHECK(platform.runtime_setup(layout, {}));
}

TEST_CASE("prefix commands run wineboot, the runtime's wineserver and Windows programs", "[dxmt_wine_runner_platform]") {
    DxmtWineRunnerPlatform platform{HostCpu::AppleSilicon};
    reboot::ports::RuntimeLayout layout;
    layout.root = NativePath{"/rt"};
    layout.entry = NativePath{"/rt/bin/wine"};
    const NativePath prefix{"/data/prefixes/mac_runtime"};

    const auto boot = platform.prefix_command(layout, prefix, {reboot::ports::PrefixVerb::Boot, {}, {}}, {});
    REQUIRE(boot);
    CHECK(boot->exe == NativePath{"/rt/bin/wine"});
    CHECK(boot->args == std::vector<std::string>{"wineboot", "-u"});
    CHECK(boot->env.vars == EnvVars{{"WINEPREFIX", "/data/prefixes/mac_runtime"}});

    const auto kill = platform.prefix_command(layout, prefix, {reboot::ports::PrefixVerb::KillServer, {}, {}}, {});
    REQUIRE(kill);
    CHECK(kill->exe == NativePath{"/rt/bin/wineserver"});
    CHECK(kill->args == std::vector<std::string>{"-k"});

    const auto run = platform.prefix_command(
        layout, prefix, {reboot::ports::PrefixVerb::Run, NativePath{"/vc/vc_redist.x64.exe"}, {"/quiet"}}, {});
    REQUIRE(run);
    CHECK(run->args == std::vector<std::string>{"/vc/vc_redist.x64.exe", "/quiet"});
    CHECK(run->cwd == NativePath{"/vc"});
}
