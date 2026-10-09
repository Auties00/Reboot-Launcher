#include <catch2/catch_message.hpp>
#include <catch2/catch_test_macros.hpp>

#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "linux_ipc_test_support.hpp"
#include "messages.hpp"
#include "reboot/foundation/paths.hpp"
#include "reboot/os_linux/ipc/linux_client_paths.hpp"
#include "reboot/os_linux/ipc/systemd_engine_starter.hpp"
#include "state_locks.hpp"

using namespace rb;
using namespace rb::os_linux::ipc;
using namespace rb::os_linux::ipc::test;

namespace {

// Stands in for reboot-engine: reports its arguments, directory and environment into the root.
constexpr std::string_view kFakeEngine = R"sh(#!/bin/sh
{
  echo "args=$*"
  echo "cwd=$(pwd)"
  env | sed 's/^/env:/'
} > "$REBOOT_LAUNCHER_HOME/report.tmp"
mv "$REBOOT_LAUNCHER_HOME/report.tmp" "$REBOOT_LAUNCHER_HOME/report"
)sh";

[[nodiscard]] LinuxClientPaths detect_paths() {
    Result<LinuxClientPaths> paths = LinuxClientPaths::detect();
    REQUIRE(paths);
    return std::move(*paths);
}

[[nodiscard]] bool locked(const NativePath& path) {
    const Result<bool> held = is_ofd_locked(path);
    REQUIRE(held);
    return *held;
}

[[nodiscard]] std::vector<std::string> lines_of(const std::string& text) {
    std::vector<std::string> lines;
    std::istringstream stream(text);
    for (std::string line; std::getline(stream, line);) lines.push_back(line);
    return lines;
}

[[nodiscard]] bool has_line(const std::vector<std::string>& lines, std::string_view line) {
    for (const std::string& candidate : lines)
        if (candidate == line) return true;
    return false;
}

[[nodiscard]] bool has_prefix(const std::vector<std::string>& lines, std::string_view prefix) {
    for (const std::string& candidate : lines)
        if (candidate.starts_with(prefix)) return true;
    return false;
}

[[nodiscard]] ports::CallerContext desktop_caller() {
    return {.os_session = "2", .elevated = false, .interactive = true, .display_env = {{"DISPLAY", ":0"}}};
}

// A runtime base outside $XDG_RUNTIME_DIR, so the systemd steps never run here.
struct StarterFixture {
    EnvOverride runtime{"XDG_RUNTIME_DIR", std::nullopt};
    testing::ScratchDir scratch = make_private_scratch("linux-starter");
    LinuxClientPaths paths = detect_paths();
    DataRoot root{scratch.path() / "data" / "root", true};
    NativePath engine_exe = scratch.path() / "bin" / "reboot-engine";

    [[nodiscard]] Result<ports::StartResult> start(ports::CallerContext caller = desktop_caller(),
                                                   bool under_steam_reaper = false) const {
        SystemdEngineStarter starter{std::move(caller), paths, under_steam_reaper};
        return starter.ensure_started(engine_exe, root);
    }

    [[nodiscard]] NativePath state_dir() const { return root.root / "state"; }
};

}  // namespace

TEST_CASE("an elevated caller is refused before anything is created", "[engine_starter]") {
    const StarterFixture fixture;
    ports::CallerContext caller = desktop_caller();
    caller.elevated = true;
    const auto result = fixture.start(caller);
    REQUIRE(result);
    CHECK(*result == ports::StartResult::ElevatedRefused);
    CHECK_FALSE(path_exists(fixture.root.root));
}

TEST_CASE("a held engine.lock is AlreadyRunning, and spawn.lock is held only during the call", "[engine_starter]") {
    const StarterFixture fixture;
    REQUIRE(create_private_dirs(fixture.state_dir()));
    const auto engine_lock = lock_ofd_exclusive(fixture.state_dir() / "engine.lock");
    REQUIRE(engine_lock);

    const auto result = fixture.start();
    REQUIRE(result);
    CHECK(*result == ports::StartResult::AlreadyRunning);
    CHECK_FALSE(locked(fixture.state_dir() / "spawn.lock"));
}

TEST_CASE("the first autostart creates the root's directories 0700", "[engine_starter]") {
    const StarterFixture fixture;
    const auto result = fixture.start(desktop_caller(), true);
    REQUIRE(result);
    CHECK(mode_of(fixture.root.root) == 0700U);
    CHECK(mode_of(fixture.state_dir()) == 0700U);
    CHECK(path_exists(fixture.state_dir() / "spawn.lock"));
}

TEST_CASE("without systemd, a Steam reaper or a non-interactive caller only connects", "[engine_starter]") {
    const StarterFixture fixture;
    const auto under_reaper = fixture.start(desktop_caller(), true);
    REQUIRE(under_reaper);
    CHECK(*under_reaper == ports::StartResult::ConnectOnly);

    ports::CallerContext service = desktop_caller();
    service.interactive = false;
    const auto headless = fixture.start(service);
    REQUIRE(headless);
    CHECK(*headless == ports::StartResult::NoInteractiveSession);
}

TEST_CASE("the self-spawned engine runs on demand with a fixed environment", "[engine_starter]") {
    const StarterFixture fixture;
    REQUIRE(create_private_dirs(fixture.engine_exe.parent_path()));
    write_text(fixture.engine_exe, kFakeEngine, 0700);
    const EnvOverride leaked{"REBOOT_LAUNCHER_TEST_LEAK", "1"};
    const EnvOverride language{"LANG", "C.UTF-8"};
    const EnvOverride config{"XDG_CONFIG_HOME", "relative/config"};
    const EnvOverride listen_pid{"LISTEN_PID", "1"};

    const auto result = fixture.start();
    REQUIRE(result);
    CHECK(*result == ports::StartResult::Started);
    CHECK_FALSE(locked(fixture.state_dir() / "spawn.lock"));

    const NativePath report_path = fixture.root.root / "report";
    REQUIRE(wait_for([&] { return path_exists(report_path); }));
    const std::string report = read_text(report_path).value_or("");
    INFO(report);
    const std::vector<std::string> lines = lines_of(report);
    CHECK(has_line(lines, "args=run --origin=on-demand"));
    CHECK(has_line(lines, "cwd=" + fixture.engine_exe.parent_path().string()));
    CHECK(has_line(lines, "env:HOME=" + fixture.paths.home().string()));
    CHECK(has_line(lines, "env:USER=" + fixture.paths.user_name()));
    CHECK(has_line(lines, "env:LOGNAME=" + fixture.paths.user_name()));
    CHECK(has_line(lines, "env:SHELL=" + fixture.paths.login_shell()));
    CHECK(has_line(lines, "env:XDG_DATA_HOME=" + fixture.paths.data_home().string()));
    CHECK(has_line(lines, "env:XDG_CACHE_HOME=" + fixture.paths.cache_home().string()));
    CHECK(has_line(lines, "env:XDG_STATE_HOME=" + fixture.paths.state_home().string()));
    CHECK(has_line(lines, "env:REBOOT_LAUNCHER_HOME=" + fixture.root.root.string()));
    CHECK(has_line(lines, "env:LANG=C.UTF-8"));
    CHECK(has_prefix(lines, "env:PATH="));
    CHECK_FALSE(has_prefix(lines, "env:REBOOT_LAUNCHER_TEST_LEAK="));
    CHECK_FALSE(has_prefix(lines, "env:XDG_CONFIG_HOME="));
    CHECK_FALSE(has_prefix(lines, "env:LISTEN_PID="));
}

TEST_CASE("an engine that cannot be executed is platform.ipc_engine_spawn_failed", "[engine_starter]") {
    const StarterFixture fixture;
    const auto result = fixture.start();
    REQUIRE_FALSE(result);
    CHECK(result.error().is(kEngineSpawnFailed));
    CHECK_FALSE(locked(fixture.state_dir() / "spawn.lock"));
}
