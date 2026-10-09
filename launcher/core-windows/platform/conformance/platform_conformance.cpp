#include "win32.hpp"  // first: sets _WIN32_WINNT before any std header pulls in <windows.h>

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstdlib>
#include <filesystem>
#include <atomic>
#include <chrono>
#include <mutex>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <thread>
#include <vector>

#include "dpapi_file.hpp"
#include "process_token.hpp"
#include "reboot/foundation/random.hpp"
#include "reboot/foundation/sha256.hpp"
#include "reboot/os_windows/platform/credential_manager_store.hpp"
#include "reboot/os_windows/platform/job_process_launcher.hpp"
#include "reboot/os_windows/platform/make_platform.hpp"
#include "reboot/os_windows/platform/unsupported_peer_inspector.hpp"
#include "reboot/os_windows/platform/velopack_applier.hpp"
#include "reboot/os_windows/platform/win32_session_host.hpp"
#include "reboot/os_windows/platform/windows_disk_info.hpp"
#include "reboot/os_windows/platform/windows_file_system.hpp"
#include "reboot/os_windows/platform/windows_file_watcher.hpp"
#include "reboot/os_windows/platform/windows_integration_registrar.hpp"
#include "reboot/os_windows/platform/windows_log_file_system.hpp"
#include "reboot/os_windows/platform/windows_paths.hpp"
#include "reboot/os_windows/platform/windows_port_inspector.hpp"
#include "reboot/os_windows/platform/windows_prerequisites.hpp"
#include "reboot/os_windows/platform/windows_resolver.hpp"
#include "reboot/os_windows/platform/windows_shell.hpp"
#include "reboot/os_windows/platform/windows_system_info.hpp"
#include "reboot/os_windows/platform/wmi_security_product_probe.hpp"
#include "reboot/testing/fake_executables.hpp"
#include "reboot/testing/fake_game_script.hpp"
#include "reboot/testing/port_binder.hpp"
#include "reboot/testing/port_conformance.hpp"
#include "reboot/testing/scratch_dir.hpp"
#include "reboot/testing/wall_clock_waiter.hpp"
#include "unique_handle.hpp"
#include "wide.hpp"

using namespace rb;
using namespace rb::os_windows::platform;
using namespace std::chrono_literals;

namespace {

void require_passed(const testing::ConformanceReport& report) {
    INFO(report.describe());
    REQUIRE(report.passed());
}

[[nodiscard]] std::vector<u8> bytes(std::string_view text) { return {text.begin(), text.end()}; }

struct Fixture {
    OsRandom random;
    testing::WallClockWaiter waiter;
    testing::ScratchDir scratch;

    [[nodiscard]] testing::ConformanceEnv env() { return {waiter, scratch.path()}; }
};

[[nodiscard]] Fixture make_fixture() {
    OsRandom random;
    auto scratch = testing::ScratchDir::create(random, "reboot-win-conformance");
    REQUIRE(scratch);
    return Fixture{{}, testing::WallClockWaiter{}, std::move(*scratch)};
}

// A directory junction needs no privilege, unlike a symbolic link.
[[nodiscard]] Result<void> make_junction(const NativePath& link, const NativePath& target) {
    const std::wstring command = L"cmd.exe /d /c mklink /J \"" + shell_path(link) + L"\" \"" + shell_path(target) + L"\" >nul";
    if (_wsystem(command.c_str()) != 0) return std::unexpected(internal_bug("mklink"));
    return {};
}

[[nodiscard]] bool process_exists(u32 pid) {
    UniqueHandle process(OpenProcess(SYNCHRONIZE, FALSE, pid));
    return process && WaitForSingleObject(process.get(), 0) == WAIT_TIMEOUT;
}

[[nodiscard]] NativePath system_tool(const wchar_t* name) {
    std::array<wchar_t, MAX_PATH> dir{};
    GetSystemDirectoryW(dir.data(), static_cast<UINT>(dir.size()));
    return NativePath(dir.data()) / name;
}

[[nodiscard]] ports::ProcessLaunch cmd(std::string script) {
    ports::ProcessLaunch launch;
    launch.exe = system_tool(L"cmd.exe");
    launch.args = {"/d", "/c", std::move(script)};
    return launch;
}

[[nodiscard]] ports::ProcessLaunch powershell(std::string script) {
    ports::ProcessLaunch launch;
    launch.exe = system_tool(L"WindowsPowerShell\\v1.0\\powershell.exe");
    launch.args = {"-NoProfile", "-NonInteractive", "-Command", std::move(script)};
    return launch;
}

}  // namespace

TEST_CASE("WindowsPaths passes the platform paths suite", "[conformance][paths]") {
    auto paths = WindowsPaths::detect();
    REQUIRE(paths);
    require_passed(testing::run_platform_paths_conformance(*paths));
    CHECK(paths->ipc_runtime_base().empty());
    CHECK(paths->default_data_root().filename() == "Reboot Launcher");
}

TEST_CASE("WindowsFileSystem passes the file system suite", "[conformance][fs]") {
    Fixture fixture = make_fixture();
    WindowsFileSystem fs;
    testing::FileSystemConformanceHooks hooks;
    hooks.make_symlink = make_junction;
    require_passed(testing::run_file_system_conformance(fs, fixture.env(), std::move(hooks)));
}

TEST_CASE("atomic_replace keeps the newest backup and survives readers", "[conformance][fs]") {
    Fixture fixture = make_fixture();
    WindowsFileSystem fs;
    const NativePath file = fixture.scratch.path() / "doc.json";
    NativePath backup = file;
    backup += ".bak";
    REQUIRE(fs.atomic_replace(file, bytes("one"), true));
    REQUIRE(fs.atomic_replace(file, bytes("two"), true));
    REQUIRE(fs.atomic_replace(file, bytes("three"), true));
    CHECK(fs.read_all(file) == bytes("three"));
    CHECK(fs.read_all(backup) == bytes("two"));

    // A reader that shares everything, as the indexer does, does not block the replace.
    UniqueHandle reader(CreateFileW(extended_path(file).c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                    nullptr, OPEN_EXISTING, 0, nullptr));
    REQUIRE(reader);
    CHECK(fs.atomic_replace(file, bytes("four"), false));
    CHECK(fs.read_all(file) == bytes("four"));
}

TEST_CASE("a held file cannot be written, renamed or deleted", "[conformance][fs]") {
    Fixture fixture = make_fixture();
    WindowsFileSystem fs;
    const NativePath file = fixture.scratch.path() / "payload.dll";
    REQUIRE(fs.atomic_replace(file, bytes("payload"), false));
    auto held = fs.open_deny_write(file);
    REQUIRE(held);
    UniqueHandle writer(CreateFileW(extended_path(file).c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                                    OPEN_EXISTING, 0, nullptr));
    CHECK_FALSE(writer);
    CHECK(DeleteFileW(extended_path(file).c_str()) == 0);
    CHECK_FALSE(fs.atomic_replace(file, bytes("swapped"), false));
    *held = ports::HeldFile{};
    CHECK(fs.atomic_replace(file, bytes("swapped"), false));
}

TEST_CASE("a waiting lock is granted once the holder releases", "[conformance][fs]") {
    Fixture fixture = make_fixture();
    WindowsFileSystem fs;
    const NativePath lock = fixture.scratch.path() / "engine.lock";
    auto first = fs.lock_exclusive(lock, false);
    REQUIRE(first);
    std::atomic<bool> granted{false};
    std::thread waiter([&] {
        WindowsFileSystem other;
        auto second = other.lock_exclusive(lock, true);
        granted = second.has_value();
    });
    std::this_thread::sleep_for(200ms);
    CHECK_FALSE(granted.load());
    first->release();
    waiter.join();
    CHECK(granted.load());
}

TEST_CASE("remove_tree clears read-only files and leaves a junction's target", "[conformance][fs]") {
    Fixture fixture = make_fixture();
    WindowsFileSystem fs;
    const NativePath tree = fixture.scratch.path() / "install";
    const NativePath kept = fixture.scratch.path() / "kept";
    REQUIRE(fs.create_dirs_owner_only(tree / "nested"));
    REQUIRE(fs.create_dirs_owner_only(kept));
    REQUIRE(fs.atomic_replace(tree / "nested" / "ro.pak", bytes("x"), false));
    REQUIRE(fs.atomic_replace(kept / "keep.txt", bytes("y"), false));
    REQUIRE(SetFileAttributesW(extended_path(tree / "nested" / "ro.pak").c_str(), FILE_ATTRIBUTE_READONLY) != 0);
    REQUIRE(make_junction(tree / "link", kept));
    REQUIRE(fs.remove_tree(tree));
    CHECK_FALSE(fs.revision(tree));
    CHECK(fs.read_all(kept / "keep.txt") == bytes("y"));
}

TEST_CASE("WindowsLogFileSystem appends, lists and removes open files", "[conformance][logs]") {
    Fixture fixture = make_fixture();
    WindowsLogFileSystem logs;
    const NativePath dir = fixture.scratch.path() / "logs" / "engine";
    REQUIRE(logs.create_directories(dir));
    REQUIRE(logs.create_directories(dir));
    auto file = logs.open_append(dir / "engine.log");
    REQUIRE(file);
    REQUIRE(file->append(bytes("hello\n")));
    REQUIRE(file->flush());
    auto reopened = logs.open_append(dir / "engine.log");
    REQUIRE(reopened);
    CHECK(reopened->size() == 6);
    REQUIRE(logs.create_directories(dir / "sub"));
    auto listed = logs.list(dir);
    REQUIRE(listed);
    REQUIRE(listed->size() == 1);
    CHECK(listed->front().path.filename() == "engine.log");
    CHECK(listed->front().size == 6);
    REQUIRE(logs.remove(dir / "engine.log"));
    CHECK(file->append(bytes("still writable\n")));
    auto after = logs.list(dir);
    REQUIRE(after);
    CHECK(after->empty());
}

TEST_CASE("WindowsFileWatcher passes the watcher suite", "[conformance][watcher]") {
    Fixture fixture = make_fixture();
    WindowsFileSystem fs;
    auto watcher = WindowsFileWatcher::create();
    REQUIRE(watcher);
    require_passed(testing::run_file_watcher_conformance(*watcher, fs, fixture.env()));
}

TEST_CASE("a watch may end from its own callback, and may outlive its watcher", "[conformance][watcher]") {
    Fixture fixture = make_fixture();
    WindowsFileSystem fs;
    std::optional<ports::WatchHandle> handle;
    std::atomic<int> calls{0};
    {
        auto watcher = WindowsFileWatcher::create();
        REQUIRE(watcher);
        std::mutex mutex;
        std::unique_lock lock(mutex);
        auto watched = watcher->watch(fixture.scratch.path(), [&](ports::FileChange) {
            std::scoped_lock inner(mutex);
            ++calls;
            handle.reset();
        });
        REQUIRE(watched);
        handle = std::move(*watched);
        lock.unlock();
        REQUIRE(fs.atomic_replace(fixture.scratch.path() / "a.txt", bytes("1"), false));
        CHECK(fixture.waiter.wait_until([&] { return calls.load() > 0; }, 5s));
        const int seen = calls.load();
        REQUIRE(fs.atomic_replace(fixture.scratch.path() / "b.txt", bytes("2"), false));
        std::this_thread::sleep_for(300ms);
        CHECK(calls.load() == seen);

        auto kept = watcher->watch(fixture.scratch.path(), [](ports::FileChange) {});
        REQUIRE(kept);
        handle = std::move(*kept);
    }
    handle.reset();
}

TEST_CASE("a watcher assigned over another stops the replaced one and keeps delivering", "[conformance][watcher]") {
    Fixture fixture = make_fixture();
    WindowsFileSystem fs;
    auto first = WindowsFileWatcher::create();
    auto second = WindowsFileWatcher::create();
    REQUIRE(first);
    REQUIRE(second);
    *first = std::move(*second);
    std::atomic<int> calls{0};
    auto watched = first->watch(fixture.scratch.path(), [&](ports::FileChange) { ++calls; });
    REQUIRE(watched);
    REQUIRE(fs.atomic_replace(fixture.scratch.path() / "moved.txt", bytes("1"), false));
    CHECK(fixture.waiter.wait_until([&] { return calls.load() > 0; }, 5s));
}

TEST_CASE("WindowsDiskInfo passes the disk info suite", "[conformance][disk]") {
    Fixture fixture = make_fixture();
    WindowsDiskInfo disk;
    require_passed(testing::run_disk_info_conformance(disk, fixture.env()));
    auto missing = disk.volume_of(fixture.scratch.path() / "not" / "made" / "yet");
    REQUIRE(missing);
    CHECK(missing->total_bytes > 0);
}

TEST_CASE("CredentialManagerStore passes the secret store suite", "[conformance][secrets]") {
    Fixture fixture = make_fixture();
    WindowsFileSystem fs;
    CredentialManagerStore store("0123456789abcdef", fixture.scratch.path() / "secrets", fs);
    require_passed(testing::run_secret_store_conformance(store, fixture.random));
}

TEST_CASE("a secret written by the other backend is found, and put removes it", "[conformance][secrets]") {
    Fixture fixture = make_fixture();
    WindowsFileSystem fs;
    const std::string hash = random_token_hex(fixture.random, 8);
    const NativePath dir = fixture.scratch.path() / "secrets";
    CredentialManagerStore store(hash, dir, fs);
    if (store.kind() != ports::SecretStoreKind::Os) {
        SKIP("this logon has no credential set");
    }
    const std::string key = "fallback-" + random_token_hex(fixture.random, 8);
    const std::string target = "RebootLauncher/" + hash + "/" + key;
    auto sealed = dpapi_seal(bytes("from a network logon"), target);
    REQUIRE(sealed);
    REQUIRE(fs.create_dirs_owner_only(dir));
    REQUIRE(fs.atomic_replace(dir / dpapi_file_name(target), *sealed, false));

    auto found = store.get(key);
    REQUIRE(found);
    REQUIRE(*found);
    CHECK((*found)->reveal() == bytes("from a network logon"));

    REQUIRE(store.put(key, bytes("now in the vault")));
    CHECK_FALSE(fs.read_all(dir / dpapi_file_name(target)));
    auto stored = store.get(key);
    REQUIRE(stored);
    REQUIRE(*stored);
    CHECK((*stored)->reveal() == bytes("now in the vault"));
    REQUIRE(store.erase(key));
}

TEST_CASE("DPAPI files only open under their own target", "[conformance][secrets]") {
    auto sealed = dpapi_seal(bytes("secret"), "RebootLauncher/a/key");
    REQUIRE(sealed);
    auto opened = dpapi_open(*sealed, "RebootLauncher/a/key");
    REQUIRE(opened);
    CHECK(opened->reveal() == bytes("secret"));
    CHECK_FALSE(dpapi_open(*sealed, "RebootLauncher/a/other"));
    CHECK(dpapi_file_name("x") == dpapi_file_name("x"));
    CHECK(dpapi_file_name("x") != dpapi_file_name("y"));
}

TEST_CASE("an oversized secret is refused", "[conformance][secrets]") {
    Fixture fixture = make_fixture();
    WindowsFileSystem fs;
    CredentialManagerStore store("0123456789abcdef", fixture.scratch.path() / "secrets", fs);
    if (store.kind() != ports::SecretStoreKind::Os) {
        SKIP("this logon has no credential set");
    }
    const std::vector<u8> large(4096, 0x41);
    const auto refused = store.put("too-large", large);
    REQUIRE_FALSE(refused);
    CHECK(refused.error().kind == ErrorKind::InvalidInput);
}

TEST_CASE("JobProcessLauncher passes the process suite, including the Job tree kill", "[conformance][process]") {
    Fixture fixture = make_fixture();
    JobProcessLauncher launcher;
    testing::ProcessConformanceSubject subject;
    subject.exits_with_7 = cmd("exit 7");
    // Quote-free: cmd.exe does not read the CRT's \" escapes.
    subject.echoes_stdin = cmd("findstr /R /C:.*");
    subject.writes_stderr = cmd("echo oops 1>&2");
    subject.prints_marker_and_cwd = cmd("echo %REBOOT_CONFORMANCE_MARKER%&cd");
    subject.spawns_grandchild = powershell(
        "$p = Start-Process -FilePath ping.exe -ArgumentList '-n','600','127.0.0.1' -PassThru -WindowStyle Hidden; "
        "[Console]::Out.WriteLine($p.Id); [Console]::Out.Flush(); $p.WaitForExit()");
    subject.process_exists = process_exists;
    testing::ConformanceEnv env = fixture.env();
    env.budget = 15s;
    require_passed(testing::run_process_launcher_conformance(launcher, std::move(subject), env));
}

TEST_CASE("destroying a child ends it, and late callbacks still get early output", "[conformance][process]") {
    Fixture fixture = make_fixture();
    JobProcessLauncher launcher;
    // The marker is written after the echo, so the callback below registers late.
    ports::ProcessLaunch waits = cmd("echo ready& type nul >ready.flag& ping -n 600 127.0.0.1 >nul");
    waits.cwd = fixture.scratch.path();
    waits.stdio = ports::StdioMode::Capture;
    auto child = launcher.spawn(waits);
    REQUIRE(child);
    const u32 pid = (*child)->pid();
    const auto created = (*child)->created();
    testing::WallClockWaiter& waiter = fixture.waiter;
    REQUIRE(waiter.wait_until([&] { return std::filesystem::exists(fixture.scratch.path() / "ready.flag"); }, 10s));
    std::mutex mutex;
    std::string out;
    (*child)->on_stdout([&](std::span<const u8> chunk) {
        std::scoped_lock lock(mutex);
        out.append(chunk.begin(), chunk.end());
    });
    CHECK(waiter.wait_until([&] {
        std::scoped_lock lock(mutex);
        return out.starts_with("ready");
    }, 5s));
    child->reset();
    CHECK(waiter.wait_until([&] { return !process_exists(pid); }, 5s));
    const auto alive = launcher.is_alive(pid, created);
    REQUIRE(alive);
    CHECK_FALSE(*alive);
    CHECK(launcher.kill(pid, created));
}

TEST_CASE("a child may be destroyed from its own exit callback", "[conformance][process]") {
    JobProcessLauncher launcher;
    ports::ProcessLaunch launch = cmd("echo bye& ping -n 2 127.0.0.1 >nul");
    launch.stdio = ports::StdioMode::Capture;
    auto spawned = launcher.spawn(launch);
    REQUIRE(spawned);
    std::unique_ptr<ports::ChildProcess> child = std::move(*spawned);
    std::atomic<bool> ended{false};
    child->on_stdout([](std::span<const u8>) {});
    child->on_exit([&](ports::ChildExit) {
        child.reset();
        ended = true;
    });
    testing::WallClockWaiter waiter;
    CHECK(waiter.wait_until([&] { return ended.load(); }, 10s));
}

TEST_CASE("a child may be destroyed from its own output callback", "[conformance][process]") {
    JobProcessLauncher launcher;
    ports::ProcessLaunch launch = cmd("echo first& ping -n 600 127.0.0.1 >nul");
    launch.stdio = ports::StdioMode::Capture;
    auto spawned = launcher.spawn(launch);
    REQUIRE(spawned);
    std::unique_ptr<ports::ChildProcess> child = std::move(*spawned);
    const u32 pid = child->pid();
    std::atomic<bool> ended{false};
    std::atomic<int> exits{0};
    child->on_exit([&](ports::ChildExit) { ++exits; });
    child->on_stdout([&](std::span<const u8>) {
        if (ended.exchange(true)) return;
        child.reset();
    });
    testing::WallClockWaiter waiter;
    REQUIRE(waiter.wait_until([&] { return ended.load(); }, 10s));
    CHECK(waiter.wait_until([&] { return !process_exists(pid); }, 5s));
    // A destroyed child reports nothing more.
    std::this_thread::sleep_for(300ms);
    CHECK(exits.load() == 0);
}

TEST_CASE("the environment overlay reaches the child over the user's base", "[conformance][process]") {
    JobProcessLauncher launcher;
    ports::ProcessLaunch launch = cmd("echo %SystemRoot%^|%REBOOT_X%");
    launch.stdio = ports::StdioMode::Capture;
    launch.env.vars = {{"REBOOT_X", "set"}};
    auto child = launcher.spawn(launch);
    REQUIRE(child);
    std::mutex mutex;
    std::string out;
    bool done = false;
    (*child)->on_stdout([&](std::span<const u8> chunk) {
        std::scoped_lock lock(mutex);
        out.append(chunk.begin(), chunk.end());
    });
    (*child)->on_exit([&](ports::ChildExit) {
        std::scoped_lock lock(mutex);
        done = true;
    });
    testing::WallClockWaiter waiter;
    REQUIRE(waiter.wait_until([&] {
        std::scoped_lock lock(mutex);
        return done;
    }, 5s));
    std::scoped_lock lock(mutex);
    CHECK(out.find("|set") != std::string::npos);
    CHECK(out.find("%SystemRoot%") == std::string::npos);
}

TEST_CASE("a missing executable fails NotFound", "[conformance][process]") {
    JobProcessLauncher launcher;
    ports::ProcessLaunch launch;
    launch.exe = NativePath(L"C:\\reboot-does-not-exist\\nothing.exe");
    launch.cwd = NativePath(L"C:\\");
    auto child = launcher.spawn(launch);
    REQUIRE_FALSE(child);
    CHECK(child.error().kind == ErrorKind::NotFound);
}

TEST_CASE("Win32SessionHost passes the session host suite with reboot-fake-game", "[conformance][session]") {
    const std::optional<NativePath> game = testing::fake_game_exe();
    if (!game) {
        SKIP("this build has no reboot-fake-game");
    }
    Fixture fixture = make_fixture();
    WindowsFileSystem fs;
    testing::FakeGameScript script;
    script.act_as_client_dll = false;
    script.exit_after = 1500ms;
    script.exit_code = 3;
    const std::string json = testing::to_json(script);
    const NativePath script_file = fixture.scratch.path() / "game.script.json";
    REQUIRE(fs.atomic_replace(script_file, bytes(json), false));

    // An Early payload goes through the deny-write hold and hash check before the spawn.
    const NativePath payload = fixture.scratch.path() / "payload.dll";
    REQUIRE(CopyFileW(extended_path(system_tool(L"winmm.dll")).c_str(), extended_path(payload).c_str(), TRUE) != 0);
    const auto payload_bytes = fs.read_all(payload);
    REQUIRE(payload_bytes);

    testing::SessionHostConformanceSubject subject;
    subject.launch.exe = *game;
    subject.launch.args = {"--script=" + narrow(script_file.native())};
    subject.launch.cwd = fixture.scratch.path();
    subject.launch.inject.push_back(
        {payload, sha256(*payload_bytes), ports::BootStrategy::EarlyBirdApc, ports::InjectPhase::Early});
    subject.process_exists = process_exists;
    Win32SessionHost host(fs);
    testing::ConformanceEnv env = fixture.env();
    env.budget = 15s;
    require_passed(testing::run_session_host_conformance(host, std::move(subject), env));
}

TEST_CASE("WindowsPortInspector passes the port suite", "[conformance][ports]") {
    WindowsPortInspector inspector;
    auto binder = testing::make_socket_port_binder();
    require_passed(testing::run_port_inspector_conformance(inspector, *binder));
    if (auto tcp = binder->bind_tcp(); tcp) {
        auto owner = inspector.tcp_owner(*tcp);
        REQUIRE(owner);
        REQUIRE(*owner);
        REQUIRE((*owner)->exe);
        CHECK((*owner)->exe->extension() == ".exe");
    }
}

TEST_CASE("UnsupportedPeerInspector passes the loopback peer suite", "[conformance][ports]") {
    UnsupportedPeerInspector inspector;
    auto binder = testing::make_socket_port_binder();
    require_passed(testing::run_loopback_peer_inspector_conformance(inspector, *binder, std::nullopt));
}

TEST_CASE("WindowsResolver passes the resolver suite", "[conformance][resolver]") {
    Fixture fixture = make_fixture();
    WindowsResolver resolver;
    require_passed(testing::run_resolver_conformance(resolver, fixture.env()));
}

TEST_CASE("WindowsShell refuses non-https URLs and recycles files", "[conformance][shell]") {
    Fixture fixture = make_fixture();
    WindowsShell shell;
    WindowsFileSystem fs;
    require_passed(testing::run_shell_launcher_conformance(shell, fs, fixture.env()));
    const auto missing = shell.trash(fixture.scratch.path() / "never-made.txt");
    REQUIRE_FALSE(missing);
    CHECK(missing.error().kind == ErrorKind::NotFound);
}

TEST_CASE("the integration registrar reads every kind without changing it", "[conformance][integration]") {
    auto sid = UserSid::current();
    REQUIRE(sid);
    auto text = sid->to_string();
    REQUIRE(text);
    WindowsIntegrationRegistrar registrar(NativePath(L"C:\\reboot-conformance-root"), *text);
    for (const auto kind : {ports::IntegrationKind::UrlScheme, ports::IntegrationKind::Autostart,
                            ports::IntegrationKind::EngineAgent, ports::IntegrationKind::DesktopEntry}) {
        const auto first = registrar.status(kind);
        REQUIRE(first);
        // Nothing under this root exists, so no entry can be ours.
        CHECK(first->state != ports::IntegrationState::Ours);
        const auto again = registrar.status(kind);
        REQUIRE(again);
        CHECK(again->state == first->state);
    }
    const auto desktop = registrar.apply(ports::IntegrationKind::DesktopEntry, NativePath(L"C:\\x.exe"));
    REQUIRE_FALSE(desktop);
    CHECK(desktop.error().kind == ErrorKind::Unsupported);
    CHECK(registrar.remove(ports::IntegrationKind::DesktopEntry));
}

TEST_CASE("WmiSecurityProductProbe passes the security probe suite", "[conformance][security]") {
    Fixture fixture = make_fixture();
    WmiSecurityProductProbe probe;
    require_passed(testing::run_security_probe_conformance(probe, fixture.env()));
}

TEST_CASE("WindowsPrerequisites passes the prerequisite suite", "[conformance][prerequisites]") {
    WindowsPrerequisites probe;
    require_passed(testing::run_prerequisite_probe_conformance(probe));
    CHECK(probe.check().front().met);
}

TEST_CASE("WindowsSystemInfo passes the system info suite", "[conformance][system]") {
    WindowsSystemInfo system;
    require_passed(testing::run_system_info_conformance(system));
    CHECK(system.os().name == "windows");
    CHECK(std::stoul(system.os().build) >= WindowsPrerequisites::kMinimumBuild);
}

TEST_CASE("VelopackApplier passes the update suite, installed or portable", "[conformance][update]") {
    Fixture fixture = make_fixture();
    VelopackApplier portable(std::nullopt, fixture.scratch.path() / "feed");
    require_passed(testing::run_update_applier_conformance(portable, fixture.env()));
    CHECK_FALSE(portable.supports_in_place());
    const auto refused = portable.apply_and_restart({"run", "--resume"});
    REQUIRE_FALSE(refused);
    CHECK(refused.error().kind == ErrorKind::Unsupported);
    VelopackApplier installed(fixture.scratch.path() / "install", fixture.scratch.path() / "feed");
    require_passed(testing::run_update_applier_conformance(installed, fixture.env()));
}

TEST_CASE("make_platform composes every Windows port", "[conformance][platform]") {
    Fixture fixture = make_fixture();
    ports::PlatformOptions options;
    options.data_root_override = fixture.scratch.path() / "data";
    auto services = ports::make_platform(options);
    REQUIRE(services);
    CHECK(services->paths);
    CHECK(services->fs);
    CHECK(services->logs);
    CHECK(services->watcher);
    CHECK(services->disk);
    CHECK(services->secrets);
    CHECK(services->processes);
    CHECK(services->session_host);
    CHECK_FALSE(services->runner);
    CHECK(services->ports);
    CHECK(services->peer_inspector);
    CHECK(services->resolver);
    CHECK_FALSE(services->ipc_listener);
    CHECK(services->shell);
    CHECK(services->integration);
    CHECK(services->security);
    CHECK(services->prerequisites);
    CHECK(services->system);
    CHECK(services->updater);
    CHECK(services->random);
    require_passed(testing::run_random_conformance(*services->random));
}
