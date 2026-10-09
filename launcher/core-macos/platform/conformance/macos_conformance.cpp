#include "darwin.hpp"

#include <catch2/catch_test_macros.hpp>

#include <signal.h>

#include <cerrno>
#include <charconv>
#include <chrono>
#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include "mac_log_file_system.hpp"
#include "reboot/foundation/random.hpp"
#include "reboot/os_macos/platform/fs_events_watcher.hpp"
#include "reboot/os_macos/platform/keychain_secret_store.hpp"
#include "reboot/os_macos/platform/libproc_port_inspector.hpp"
#include "reboot/os_macos/platform/mac_disk_info.hpp"
#include "reboot/os_macos/platform/mac_file_system.hpp"
#include "reboot/os_macos/platform/mac_paths.hpp"
#include "reboot/os_macos/platform/mac_prerequisite_probe.hpp"
#include "reboot/os_macos/platform/mac_resolver.hpp"
#include "reboot/os_macos/platform/mac_shell.hpp"
#include "reboot/os_macos/platform/mac_system_info.hpp"
#include "reboot/os_macos/platform/mac_velopack_applier.hpp"
#include "reboot/os_macos/platform/make_platform.hpp"
#include "reboot/os_macos/platform/posix_spawn_launcher.hpp"
#include "reboot/os_macos/platform/unsupported_peer_inspector.hpp"
#include "reboot/testing/port_binder.hpp"
#include "reboot/testing/port_conformance.hpp"
#include "reboot/testing/scratch_dir.hpp"
#include "reboot/testing/wall_clock_waiter.hpp"

using namespace rb;
using namespace rb::os_macos::platform;
using namespace std::chrono_literals;

namespace {

void require_passed(const testing::ConformanceReport& report) {
    INFO(report.suite() << ": " << report.describe());
    CHECK(report.passed());
}

struct Scratch {
    OsRandom random;
    testing::WallClockWaiter waiter;
    testing::ScratchDir dir;

    Scratch() : dir(make_dir(random)) {}

    [[nodiscard]] testing::ConformanceEnv env() { return testing::ConformanceEnv{waiter, dir.path(), 5000ms}; }

private:
    static testing::ScratchDir make_dir(IRandom& random) {
        Result<testing::ScratchDir> created = testing::ScratchDir::create(random, "reboot-macos-conformance");
        REQUIRE(created);
        return std::move(*created);
    }
};

[[nodiscard]] bool process_exists(u32 pid) { return ::kill(static_cast<pid_t>(pid), 0) == 0 || errno == EPERM; }

[[nodiscard]] ports::ProcessLaunch shell(std::string script) {
    ports::ProcessLaunch launch;
    launch.exe = "/bin/sh";
    launch.args = {"-c", std::move(script)};
    launch.env.vars.emplace_back("PATH", "/usr/bin:/bin");
    return launch;
}

// Collects a child's stdout behind a mutex, since it arrives on the launcher's kqueue thread.
struct Output {
    std::mutex mutex;
    std::string text;

    [[nodiscard]] std::optional<u32> first_pid() {
        std::scoped_lock lock(mutex);
        const std::size_t end = text.find('\n');
        if (end == std::string::npos) return std::nullopt;
        u32 pid = 0;
        const auto [ptr, error] = std::from_chars(text.data(), text.data() + end, pid);
        if (error != std::errc{} || pid == 0) return std::nullopt;
        return pid;
    }
};

[[nodiscard]] std::optional<u32> grandchild_of(ports::ChildProcess& child, testing::IConformanceWaiter& waiter) {
    auto output = std::make_shared<Output>();
    child.on_stdout([output](std::span<const u8> bytes) {
        std::scoped_lock lock(output->mutex);
        output->text.append(bytes.begin(), bytes.end());
    });
    std::optional<u32> pid;
    (void)waiter.wait_until([&] { return (pid = output->first_pid()).has_value(); }, 5000ms);
    return pid;
}

}  // namespace

TEST_CASE("MacPaths meets the platform paths contract", "[conformance]") {
    Result<MacPaths> paths = MacPaths::detect();
    REQUIRE(paths);
    require_passed(testing::run_platform_paths_conformance(*paths));
    CHECK(paths->ipc_runtime_base().native().starts_with("/var/folders/"));
}

TEST_CASE("MacFileSystem meets the file system contract", "[conformance]") {
    Scratch scratch;
    MacFileSystem fs;
    testing::FileSystemConformanceHooks hooks;
    hooks.make_symlink = [](const NativePath& link, const NativePath& target) -> Result<void> {
        std::error_code error;
        std::filesystem::create_directory_symlink(target, link, error);
        if (error) return std::unexpected(internal_bug("create_directory_symlink"));
        return {};
    };
    require_passed(testing::run_file_system_conformance(fs, scratch.env(), std::move(hooks)));
}

TEST_CASE("FsEventsWatcher meets the file watcher contract", "[conformance]") {
    Scratch scratch;
    MacFileSystem fs;
    FsEventsWatcher watcher;
    require_passed(testing::run_file_watcher_conformance(watcher, fs, scratch.env()));
}

TEST_CASE("MacDiskInfo meets the disk info contract", "[conformance]") {
    Scratch scratch;
    MacDiskInfo disk;
    require_passed(testing::run_disk_info_conformance(disk, scratch.env()));
}

TEST_CASE("KeychainSecretStore meets the secret store contract", "[conformance]") {
    Scratch scratch;
    MacFileSystem fs;
    // Outside Aqua a locked keychain falls back to files, so the suite runs on any runner.
    KeychainSecretStore store("0123456789abcdef", scratch.dir.path() / "secrets", fs, false);
    require_passed(testing::run_secret_store_conformance(store, scratch.random));
}

TEST_CASE("PosixSpawnLauncher meets the process launcher contract", "[conformance]") {
    Scratch scratch;
    PosixSpawnLauncher launcher;
    testing::ProcessConformanceSubject subject;
    subject.exits_with_7 = shell("exit 7");
    subject.echoes_stdin = shell("exec cat");
    subject.writes_stderr = shell("echo 'conformance stderr' >&2");
    subject.prints_marker_and_cwd = shell("printf '%s\\n' \"$REBOOT_CONFORMANCE_MARKER\"; pwd");
    subject.spawns_grandchild = shell("sleep 600 & echo $!; wait");
    subject.process_exists = process_exists;
    require_passed(testing::run_process_launcher_conformance(launcher, std::move(subject), scratch.env()));
}

TEST_CASE("destroying a child kills its process group through the watchdog", "[conformance]") {
    testing::WallClockWaiter waiter;
    PosixSpawnLauncher launcher;
    ports::ProcessLaunch launch = shell("sleep 600 & echo $!; wait");
    launch.stdio = ports::StdioMode::Capture;
    Result<std::unique_ptr<ports::ChildProcess>> child = launcher.spawn(launch);
    REQUIRE(child);
    const u32 pid = (*child)->pid();
    const std::optional<u32> grandchild = grandchild_of(**child, waiter);
    REQUIRE(grandchild);
    CHECK(process_exists(*grandchild));
    child->reset();
    CHECK(waiter.wait_until([&] { return !process_exists(*grandchild) && !process_exists(pid); }, 5000ms));
}

TEST_CASE("a child signalling its own group cannot end the watchdog", "[conformance]") {
    testing::WallClockWaiter waiter;
    PosixSpawnLauncher launcher;
    // The pause lets the watchdog join the group and set its traps first.
    ports::ProcessLaunch launch = shell("trap '' TERM; sleep 1; kill -TERM 0; sleep 600 & echo $!; wait");
    launch.stdio = ports::StdioMode::Capture;
    Result<std::unique_ptr<ports::ChildProcess>> child = launcher.spawn(launch);
    REQUIRE(child);
    const u32 pid = (*child)->pid();
    const std::optional<u32> grandchild = grandchild_of(**child, waiter);
    REQUIRE(grandchild);
    child->reset();
    CHECK(waiter.wait_until([&] { return !process_exists(*grandchild) && !process_exists(pid); }, 5000ms));
}

TEST_CASE("destroying a child outside its own group kills that pid", "[conformance]") {
    testing::WallClockWaiter waiter;
    PosixSpawnLauncher launcher;
    ports::ProcessLaunch launch = shell("exec sleep 600");
    launch.own_group = false;
    Result<std::unique_ptr<ports::ChildProcess>> child = launcher.spawn(launch);
    REQUIRE(child);
    const u32 pid = (*child)->pid();
    CHECK(process_exists(pid));
    child->reset();
    CHECK(waiter.wait_until([&] { return !process_exists(pid); }, 5000ms));
}

TEST_CASE("a child that exits on its own reports once, after its output", "[conformance]") {
    testing::WallClockWaiter waiter;
    PosixSpawnLauncher launcher;
    ports::ProcessLaunch launch = shell("echo first; echo second; exit 3");
    launch.own_group = false;
    launch.stdio = ports::StdioMode::Capture;
    Result<std::unique_ptr<ports::ChildProcess>> child = launcher.spawn(launch);
    REQUIRE(child);
    struct Seen {
        std::mutex mutex;
        std::string out;
        std::string out_at_exit;
        int exits = 0;
        std::optional<int> code;
    };
    auto seen = std::make_shared<Seen>();
    (*child)->on_stdout([seen](std::span<const u8> bytes) {
        std::scoped_lock lock(seen->mutex);
        seen->out.append(bytes.begin(), bytes.end());
    });
    (*child)->on_exit([seen](ports::ChildExit exit) {
        std::scoped_lock lock(seen->mutex);
        ++seen->exits;
        seen->code = exit.code;
        seen->out_at_exit = seen->out;
    });
    REQUIRE(waiter.wait_until([&] { std::scoped_lock lock(seen->mutex); return seen->exits > 0; }, 5000ms));
    (void)waiter.wait_until([] { return false; }, 300ms);
    std::scoped_lock lock(seen->mutex);
    CHECK(seen->exits == 1);
    CHECK(seen->code == 3);
    CHECK(seen->out_at_exit == "first\nsecond\n");
}

TEST_CASE("LibprocPortInspector meets the port inspector contract", "[conformance]") {
    LibprocPortInspector inspector;
    std::unique_ptr<testing::IPortBinder> binder = testing::make_socket_port_binder();
    require_passed(testing::run_port_inspector_conformance(inspector, *binder));
}

TEST_CASE("the loopback peer check is not supported on macOS", "[conformance]") {
    UnsupportedPeerInspector inspector;
    std::unique_ptr<testing::IPortBinder> binder = testing::make_socket_port_binder();
    require_passed(testing::run_loopback_peer_inspector_conformance(inspector, *binder, std::nullopt));
}

TEST_CASE("MacResolver meets the resolver contract", "[conformance]") {
    Scratch scratch;
    MacResolver resolver;
    require_passed(testing::run_resolver_conformance(resolver, scratch.env()));
}

TEST_CASE("MacShell refuses non-https links and trashes files", "[conformance]") {
    Scratch scratch;
    MacFileSystem fs;
    MacShell shell_launcher(true);
    require_passed(testing::run_shell_launcher_conformance(shell_launcher, fs, scratch.env()));
}

TEST_CASE("MacShell outside Aqua opens nothing", "[conformance]") {
    MacShell shell_launcher(false);
    const Result<void> opened = shell_launcher.open_url("https://example.com");
    REQUIRE_FALSE(opened);
    CHECK(opened.error().id == "platform.no_gui_session");
    CHECK_FALSE(shell_launcher.reveal("/"));
}

TEST_CASE("MacSystemInfo reports the OS and exports the trust anchors", "[conformance]") {
    Scratch scratch;
    MacFileSystem fs;
    const MacSystemInfo system(scratch.dir.path() / "trust", fs);
    require_passed(testing::run_system_info_conformance(system));
    CHECK(system.os().name == "macOS");
    CHECK_FALSE(system.os_session().empty());
    REQUIRE(system.ca_bundle());
    const Result<std::vector<u8>> pem = fs.read_all(*system.ca_bundle());
    REQUIRE(pem);
    CHECK(std::string(pem->begin(), pem->end()).starts_with("-----BEGIN CERTIFICATE-----\n"));
}

TEST_CASE("MacPrerequisiteProbe meets the prerequisite contract", "[conformance]") {
    MacPrerequisiteProbe probe(std::nullopt);
    require_passed(testing::run_prerequisite_probe_conformance(probe));
}

TEST_CASE("MacLogFileSystem appends, lists regular files and refuses links", "[conformance]") {
    Scratch scratch;
    MacLogFileSystem logs;
    const NativePath dir = scratch.dir.path() / "logs" / "nested";
    REQUIRE(logs.create_directories(dir));
    {
        Result<ports::LogFile> file = logs.open_append(dir / "engine.log");
        REQUIRE(file);
        const std::string_view text = "one\ntwo\n";
        REQUIRE(file->append(std::span<const u8>(reinterpret_cast<const u8*>(text.data()), text.size())));
        REQUIRE(file->flush());
    }
    std::error_code error;
    std::filesystem::create_symlink(dir / "engine.log", dir / "link.log", error);
    REQUIRE_FALSE(error);
    Result<std::vector<ports::LogDirEntry>> listed = logs.list(dir);
    REQUIRE(listed);
    REQUIRE(listed->size() == 1);
    CHECK(listed->front().path == dir / "engine.log");
    CHECK(listed->front().size == 8);
    CHECK_FALSE(logs.open_append(dir / "link.log"));
    REQUIRE(logs.remove(dir / "engine.log"));
    CHECK_FALSE(logs.remove(dir / "engine.log"));
}

TEST_CASE("make_platform composes every engine port macOS provides", "[conformance]") {
    Scratch scratch;
    ports::PlatformOptions options;
    options.data_root_override = scratch.dir.path() / "data";
    options.foreground = true;
    Result<ports::PlatformServices> services = ports::make_platform(options);
    REQUIRE(services);
    CHECK(services->paths);
    CHECK(services->fs);
    CHECK(services->logs);
    CHECK(services->watcher);
    CHECK(services->disk);
    CHECK(services->secrets);
    CHECK(services->processes);
    CHECK_FALSE(services->session_host);
    CHECK_FALSE(services->runner);
    CHECK_FALSE(services->ipc_listener);
    CHECK(services->ports);
    CHECK(services->peer_inspector);
    CHECK(services->resolver);
    CHECK(services->shell);
    CHECK(services->integration);
    CHECK(services->security);
    CHECK(services->prerequisites);
    CHECK(services->system);
    CHECK(services->updater);
    CHECK(services->random);
    // The test binary runs from no bundle.
    CHECK_FALSE(services->updater->supports_in_place());
}

TEST_CASE("MacVelopackApplier outside a bundle stages nothing", "[conformance]") {
    Scratch scratch;
    MacVelopackApplier updater(std::nullopt, scratch.dir.path() / "feed", std::nullopt);
    CHECK_FALSE(updater.supports_in_place());
    require_passed(testing::run_update_applier_conformance(updater, scratch.env()));
}
