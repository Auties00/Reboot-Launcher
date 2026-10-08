#include <catch2/catch_message.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>

#include "messages.hpp"
#include "reboot/foundation/cancel.hpp"
#include "reboot/foundation/random.hpp"
#include "reboot/os_linux/platform/pidfd_process_launcher.hpp"
#include "reboot/os_linux/runner/kron_wine_runner.hpp"
#include "reboot/os_linux/runner/linux_runner_platform.hpp"
#include "reboot/os_linux/runner/slr_build.hpp"
#include "reboot/os_linux/runner/slr_setup.hpp"
#include "reboot/os_linux/runner/umu_invocation.hpp"
#include "reboot/testing/port_conformance.hpp"
#include "reboot/testing/scratch_dir.hpp"

namespace fs = std::filesystem;
using reboot::NativePath;
using reboot::os_linux::runner::KronWineRunner;
using reboot::os_linux::runner::LinuxRunnerPlatform;
using reboot::os_linux::runner::SlrBuild;
using reboot::os_linux::runner::SlrSetup;
using reboot::os_linux::runner::UmuInvocation;
using reboot::ports::RunnerKind;

namespace {

constexpr std::string_view kToolManifest = "\"manifest\"\n{\n  \"require_tool_appid\" \"1628350\"\n}\n";

// Stands in for umu-run: checks the setup contract, then writes what umu leaves behind.
constexpr std::string_view kFakeUmuRun = R"(#!/bin/sh
echo "runtime update $UMU_RUNTIME_UPDATE"
[ "$#" -eq 1 ] && [ -z "$1" ] || exit 9
[ "$UMU_RUNTIME_UPDATE" = 1 ] && [ -d "$WINEPREFIX" ] || exit 8
[ -n "$FAKE_SLEEP" ] && exec sleep 30
mkdir -p "$UMU_FOLDERS_PATH/steamrt3"
printf 'depot\t0.20240806.99006\t-\t-\n' > "$UMU_FOLDERS_PATH/steamrt3/VERSIONS.txt"
exit "${FAKE_EXIT:-0}"
)";

void write_file(const NativePath& path, std::string_view text, bool executable = false) {
    fs::create_directories(path.parent_path());
    std::ofstream{path} << text;
    if (executable) fs::permissions(path, fs::perms::owner_all, fs::perm_options::add);
}

reboot::testing::ScratchDir scratch() {
    reboot::OsRandom random;
    auto dir = reboot::testing::ScratchDir::create(random, "linux-runner");
    REQUIRE(dir);
    return std::move(*dir);
}

void write_wine(const NativePath& root) {
    write_file(root / KronWineRunner::kWineLoader, "", true);
    write_file(root / KronWineRunner::kWineServer, "", true);
    fs::create_directories(root / KronWineRunner::kWindowsDllDir);
}

// GE-Proton with a wineserver that has nothing to stop, and the fake umu-run.
reboot::ports::RuntimeDirs write_umu_runtimes(const NativePath& dir) {
    write_file(dir / "proton" / UmuInvocation::kProtonScript, "", true);
    write_file(dir / "proton" / "toolmanifest.vdf", kToolManifest);
    write_file(dir / "proton" / "files/bin/wineserver", "#!/bin/sh\nexit 0\n", true);
    write_file(dir / "umu" / UmuInvocation::kUmuRun, kFakeUmuRun, true);
    return {dir / "proton", dir / "umu"};
}

reboot::ports::RuntimeLayout write_umu(const NativePath& dir) {
    const reboot::ports::RuntimeDirs dirs = write_umu_runtimes(dir);
    const auto umu = UmuInvocation::resolve(*dirs.launcher, dirs.runtime, dir / "folders");
    REQUIRE(umu);
    return umu->to_runtime_layout();
}

reboot::ports::EnvBlock base_env(std::optional<std::pair<std::string, std::string>> extra = std::nullopt) {
    reboot::ports::EnvBlock env{{{"PATH", "/usr/bin:/bin"}}};
    if (extra) env.vars.push_back(std::move(*extra));
    return env;
}

}  // namespace

TEST_CASE("a complete Wine runtime resolves", "[runner_conformance]") {
    const auto dir = scratch();
    write_wine(dir.path());
    const auto runner = KronWineRunner::resolve(dir.path());
    REQUIRE(runner);
    CHECK(runner->wine == dir.path() / KronWineRunner::kWineLoader);
}

TEST_CASE("a Wine archive's top-level folder is the root", "[runner_conformance]") {
    const auto dir = scratch();
    write_wine(dir.path() / "wine-11.0-amd64-wow64");
    const auto runner = KronWineRunner::resolve(dir.path());
    REQUIRE(runner);
    CHECK(runner->root == dir.path() / "wine-11.0-amd64-wow64");
}

TEST_CASE("a missing wineserver is named", "[runner_conformance]") {
    const auto dir = scratch();
    write_wine(dir.path());
    fs::remove(dir.path() / KronWineRunner::kWineServer);
    const auto runner = KronWineRunner::resolve(dir.path());
    REQUIRE_FALSE(runner);
    CHECK(runner.error().is(reboot::os_linux::runner::kWineMissing));
    const reboot::Arg* file = runner.error().find_arg("file");
    REQUIRE(file);
    CHECK(std::get<std::string>(*file) == KronWineRunner::kWineServer);
}

TEST_CASE("umu needs an executable umu-run and a proton script", "[runner_conformance]") {
    const auto dir = scratch();
    write_umu(dir.path());
    fs::permissions(dir.path() / "umu" / UmuInvocation::kUmuRun, fs::perms::owner_read);
    const auto no_umu = UmuInvocation::resolve(dir.path() / "umu", dir.path() / "proton", dir.path() / "folders");
    REQUIRE_FALSE(no_umu);
    CHECK(no_umu.error().is(reboot::os_linux::runner::kUmuRunMissing));

    fs::permissions(dir.path() / "umu" / UmuInvocation::kUmuRun, fs::perms::owner_all);
    fs::remove(dir.path() / "proton" / UmuInvocation::kProtonScript);
    const auto no_proton = UmuInvocation::resolve(dir.path() / "umu", dir.path() / "proton", dir.path() / "folders");
    REQUIRE_FALSE(no_proton);
    CHECK(no_proton.error().is(reboot::os_linux::runner::kProtonMissing));
}

TEST_CASE("the platform lays out umu from GE-Proton and the umu launcher", "[runner_conformance]") {
    const auto dir = scratch();
    const reboot::ports::RuntimeDirs dirs = write_umu_runtimes(dir.path());
    reboot::os_linux::platform::PidfdProcessLauncher processes{std::nullopt};
    LinuxRunnerPlatform platform{processes, base_env(), dir.path() / "folders"};

    const auto layout = platform.layout(RunnerKind::Umu, dirs);
    REQUIRE(layout);
    CHECK(layout->entry == dir.path() / "umu" / UmuInvocation::kUmuRun);
    CHECK(layout->root == dir.path() / "proton");
    const std::pair<std::string, std::string> folders{"UMU_FOLDERS_PATH", (dir.path() / "folders").string()};
    CHECK(std::ranges::contains(layout->env, folders));

    const auto report = reboot::testing::run_runner_platform_conformance(
        platform, {RunnerKind::Umu, dirs, dir.path() / "prefix", dir.path() / "winhost" / "reboot-winhost.exe"});
    INFO(report.describe());
    CHECK(report.passed());
}

TEST_CASE("the platform lays out Wine from its runtime", "[runner_conformance]") {
    const auto dir = scratch();
    write_wine(dir.path() / "wine");
    reboot::os_linux::platform::PidfdProcessLauncher processes{std::nullopt};
    LinuxRunnerPlatform platform{processes, base_env(), dir.path() / "folders"};

    const auto report = reboot::testing::run_runner_platform_conformance(
        platform, {RunnerKind::Wine, {dir.path() / "wine"}, dir.path() / "prefix", dir.path() / "reboot-winhost.exe"});
    INFO(report.describe());
    CHECK(report.passed());
}

TEST_CASE("the setup installs the runtime and reports its build", "[runner_conformance]") {
    const auto dir = scratch();
    const auto layout = write_umu(dir.path());
    reboot::os_linux::platform::PidfdProcessLauncher processes{std::nullopt};
    SlrSetup setup{processes, base_env(), dir.path() / "folders"};

    const auto build = setup.run(layout, {});
    REQUIRE(build);
    CHECK(*build == SlrBuild{"steamrt3", "0.20240806.99006"});
    CHECK_FALSE(fs::exists(dir.path() / "folders" / SlrSetup::kScratchPrefix));
}

TEST_CASE("a failed setup reports umu-run's exit code", "[runner_conformance]") {
    const auto dir = scratch();
    const auto layout = write_umu(dir.path());
    reboot::os_linux::platform::PidfdProcessLauncher processes{std::nullopt};
    SlrSetup setup{processes, base_env(std::pair{"FAKE_EXIT", "3"}), dir.path() / "folders"};

    const auto build = setup.run(layout, {});
    REQUIRE_FALSE(build);
    CHECK(build.error().is(reboot::os_linux::runner::kSlrSetupFailed));
    CHECK(!fs::exists(dir.path() / "folders" / SlrSetup::kScratchPrefix));
}

TEST_CASE("a cancelled setup kills umu-run", "[runner_conformance]") {
    const auto dir = scratch();
    const auto layout = write_umu(dir.path());
    reboot::os_linux::platform::PidfdProcessLauncher processes{std::nullopt};
    SlrSetup setup{processes, base_env(std::pair{"FAKE_SLEEP", "1"}), dir.path() / "folders"};
    reboot::CancelSource cancel;
    cancel.cancel(reboot::CancelReason::User);

    const auto build = setup.run(layout, cancel.token());
    REQUIRE_FALSE(build);
    CHECK(build.error().is(reboot::os_linux::runner::kSlrSetupCancelled));
}
