#include <catch2/catch_message.hpp>
#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <cerrno>
#include <cstdlib>
#include <fcntl.h>
#include <filesystem>
#include <fstream>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <sys/stat.h>
#include <system_error>
#include <thread>
#include <unistd.h>
#include <utility>
#include <vector>

#include "conformance_support.hpp"
#include "linux_log_file_system.hpp"
#include "messages.hpp"
#include "reboot/foundation/paths.hpp"
#include "reboot/os_linux/platform/inotify_watcher.hpp"
#include "reboot/os_linux/platform/libsecret_secret_store.hpp"
#include "reboot/os_linux/platform/linux_disk_info.hpp"
#include "reboot/os_linux/platform/linux_file_system.hpp"
#include "reboot/os_linux/platform/linux_integration_registrar.hpp"
#include "reboot/os_linux/platform/linux_prerequisite_probe.hpp"
#include "reboot/os_linux/platform/linux_resolver.hpp"
#include "reboot/os_linux/platform/linux_system_info.hpp"
#include "reboot/os_linux/platform/linux_update_applier.hpp"
#include "reboot/os_linux/platform/make_platform.hpp"
#include "reboot/os_linux/platform/sock_diag_peer_inspector.hpp"
#include "reboot/os_linux/platform/sock_diag_port_inspector.hpp"
#include "reboot/os_linux/platform/tarball_layout.hpp"
#include "reboot/os_linux/platform/xdg_paths.hpp"
#include "reboot/os_linux/platform/xdg_shell.hpp"
#include "reboot/testing/port_binder.hpp"

using namespace rb;
using namespace rb::os_linux::platform;
using rb::os_linux::platform::test::require_passed;
using rb::os_linux::platform::test::require_passed_except;
using rb::os_linux::platform::test::Scratch;
namespace fs = std::filesystem;

namespace {

[[nodiscard]] std::span<const u8> bytes_of(std::string_view text) {
    return {reinterpret_cast<const u8*>(text.data()), text.size()};
}

struct Changes {
    std::mutex mutex;
    std::vector<ports::FileChange> seen;

    [[nodiscard]] bool has(const NativePath& path, ports::FileChangeKind kind) {
        const std::lock_guard lock(mutex);
        for (const ports::FileChange& change : seen)
            if (change.path == path && change.kind == kind) return true;
        return false;
    }
};

[[nodiscard]] UniqueFunction<void(ports::FileChange)> record_into(const std::shared_ptr<Changes>& changes) {
    return [changes](ports::FileChange change) {
        const std::lock_guard lock(changes->mutex);
        changes->seen.push_back(std::move(change));
    };
}

}  // namespace

TEST_CASE("XdgPaths resolves absolute, normal roots under the XDG homes", "[linux_conformance]") {
    const Result<XdgPaths> paths = XdgPaths::detect();
    REQUIRE(paths);
    require_passed(testing::run_platform_paths_conformance(*paths));
    CHECK(paths->default_data_root() == paths->data_home() / "reboot-launcher");
    CHECK(paths->default_logs_root() == paths->state_home() / "reboot-launcher" / "logs");
    CHECK(paths->uid() == static_cast<u32>(::geteuid()));
    CHECK_FALSE(paths->user_name().empty());
    if (paths->runtime_dir()) CHECK(paths->ipc_runtime_base() == *paths->runtime_dir());
    // The test binary runs from a build tree.
    CHECK(paths->install_kind() == ports::InstallKind::Dev);
    const Result<EngineCommand> engine = stable_engine_command(*paths);
    REQUIRE_FALSE(engine);
    CHECK(engine.error().kind == ErrorKind::Unsupported);
}

TEST_CASE("LinuxFileSystem passes the file system suite", "[linux_conformance]") {
    Scratch scratch;
    LinuxFileSystem fs;
    testing::FileSystemConformanceHooks hooks;
    hooks.make_symlink = [](const NativePath& link, const NativePath& target) -> Result<void> {
        if (::symlink(target.c_str(), link.c_str()) != 0) return std::unexpected(internal_bug("symlink"));
        return {};
    };
    require_passed(testing::run_file_system_conformance(fs, scratch.env(), std::move(hooks)));
}

TEST_CASE("an OFD lock survives another fd of the same file closing", "[linux_conformance]") {
    Scratch scratch;
    LinuxFileSystem fs;
    const NativePath lock_path = scratch.dir.path() / "engine.lock";
    Result<ports::FileLock> held = fs.lock_exclusive(lock_path, false);
    REQUIRE(held);
    // A classic POSIX record lock would be dropped here.
    const int unrelated = ::open(lock_path.c_str(), O_RDONLY | O_CLOEXEC);
    REQUIRE(unrelated >= 0);
    ::close(unrelated);
    const Result<ports::FileLock> second = fs.lock_exclusive(lock_path, false);
    REQUIRE_FALSE(second);
    CHECK(second.error().kind == ErrorKind::Conflict);
    CHECK(second.error().is(kLockBusy));
    held->release();
    CHECK(fs.lock_exclusive(lock_path, false));
}

TEST_CASE("InotifyWatcher passes the watcher suite", "[linux_conformance]") {
    Scratch scratch;
    LinuxFileSystem fs;
    InotifyWatcher watcher;
    require_passed(testing::run_file_watcher_conformance(watcher, fs, scratch.env()));
}

TEST_CASE("inotify reports creation, removal and renames, and two watches share a directory", "[linux_conformance]") {
    Scratch scratch;
    InotifyWatcher watcher;
    const NativePath dir = scratch.dir.path() / "watched";
    fs::create_directory(dir);
    auto first = std::make_shared<Changes>();
    auto second = std::make_shared<Changes>();
    auto first_handle = watcher.watch(dir, record_into(first));
    REQUIRE(first_handle);
    auto second_handle = watcher.watch(dir, record_into(second));
    REQUIRE(second_handle);

    const NativePath file = dir / "a.txt";
    { std::ofstream{file} << "x"; }
    fs::rename(file, dir / "b.txt");
    fs::remove(dir / "b.txt");
    CHECK(scratch.waiter.wait_until([&] { return first->has(file, ports::FileChangeKind::Created); }, std::chrono::seconds{5}));
    CHECK(scratch.waiter.wait_until([&] { return first->has(dir / "b.txt", ports::FileChangeKind::Renamed); },
                                    std::chrono::seconds{5}));
    CHECK(scratch.waiter.wait_until([&] { return first->has(dir / "b.txt", ports::FileChangeKind::Removed); },
                                    std::chrono::seconds{5}));
    CHECK(scratch.waiter.wait_until([&] { return second->has(file, ports::FileChangeKind::Created); }, std::chrono::seconds{5}));

    // The kernel watch is shared: dropping one handle leaves the other reporting.
    *first_handle = ports::WatchHandle{};
    { std::ofstream{dir / "c.txt"} << "y"; }
    CHECK(scratch.waiter.wait_until([&] { return second->has(dir / "c.txt", ports::FileChangeKind::Created); },
                                    std::chrono::seconds{5}));

    fs::remove(dir / "c.txt");
    fs::remove(dir);
    CHECK(scratch.waiter.wait_until([&] { return second->has(dir, ports::FileChangeKind::Removed); }, std::chrono::seconds{5}));
}

TEST_CASE("LinuxDiskInfo passes the disk suite", "[linux_conformance]") {
    Scratch scratch;
    LinuxDiskInfo disk;
    require_passed(testing::run_disk_info_conformance(disk, scratch.env()));
    const Result<std::vector<ports::VolumeInfo>> volumes = disk.volumes();
    REQUIRE(volumes);
    for (const ports::VolumeInfo& volume : *volumes) {
        CHECK(volume.fs_type != "proc");
        CHECK(volume.fs_type != "tmpfs");
    }
}

TEST_CASE("LibsecretSecretStore passes the secret store suite", "[linux_conformance]") {
    Scratch scratch;
    LinuxFileSystem fs;
    LibsecretSecretStore store("0123456789abcdef", scratch.dir.path() / "state" / "secrets", fs);
    CHECK(store.kind() != ports::SecretStoreKind::Unavailable);
    require_passed(testing::run_secret_store_conformance(store, scratch.random));
}

TEST_CASE("fallback secrets are 0600 files in a 0700 directory", "[linux_conformance]") {
    Scratch scratch;
    LinuxFileSystem fs;
    const NativePath dir = scratch.dir.path() / "state" / "secrets";
    LibsecretSecretStore store("0123456789abcdef", dir, fs);
    if (store.kind() != ports::SecretStoreKind::File) SKIP("a Secret Service answers here");
    const std::vector<u8> value{1, 2, 3};
    REQUIRE(store.put("account/1", value));
    struct stat info {};
    REQUIRE(::stat(dir.c_str(), &info) == 0);
    CHECK((info.st_mode & 0777) == 0700);
    std::size_t files = 0;
    for (const auto& entry : fs::directory_iterator(dir)) {
        REQUIRE(::stat(entry.path().c_str(), &info) == 0);
        CHECK((info.st_mode & 0777) == 0600);
        ++files;
    }
    CHECK(files == 1);
    REQUIRE(store.erase("account/1"));
    CHECK(store.erase("account/1"));
    CHECK(fs::is_empty(dir));
}

TEST_CASE("LinuxSystemInfo passes the system info suite", "[linux_conformance]") {
    const LinuxSystemInfo system;
    const testing::ConformanceReport report = testing::run_system_info_conformance(system);
    // CI runs as a service, where no login session is set.
    if (std::getenv("XDG_SESSION_ID") == nullptr)
        require_passed_except(report, {"the os session is not empty"});
    else
        require_passed(report);
    CHECK(system.elevated() == (::geteuid() == 0));
    if (const auto bundle = system.ca_bundle()) CHECK(fs::file_size(*bundle) > 0);
}

TEST_CASE("LinuxPrerequisiteProbe passes the prerequisite suite", "[linux_conformance]") {
    const Result<XdgPaths> paths = XdgPaths::detect();
    REQUIRE(paths);
    LinuxPrerequisiteProbe probe(paths->user_name());
    require_passed(testing::run_prerequisite_probe_conformance(probe));
    const auto remedied = probe.remediate(LinuxPrerequisiteProbe::kPython3Id);
    REQUIRE_FALSE(remedied);
    CHECK(remedied.error().is(kNoRemediation));
}

TEST_CASE("LinuxUpdateApplier is notify-only outside a self-installed copy", "[linux_conformance]") {
    Scratch scratch;
    const Result<XdgPaths> paths = XdgPaths::detect();
    REQUIRE(paths);
    LinuxUpdateApplier updater(*paths, false);
    require_passed(testing::run_update_applier_conformance(updater, scratch.env()));
    CHECK_FALSE(updater.supports_in_place());
    const auto applied = updater.apply_and_restart({"run"});
    REQUIRE_FALSE(applied);
    CHECK(applied.error().is(kUpdateNotifyOnly));
    LinuxUpdateApplier contained(*paths, true);
    CHECK_FALSE(contained.supports_in_place());
}

TEST_CASE("LinuxIntegrationRegistrar refuses a dev tree", "[linux_conformance]") {
    const Result<XdgPaths> paths = XdgPaths::detect();
    REQUIRE(paths);
    LinuxIntegrationRegistrar registrar(*paths, "0123456789abcdef", std::nullopt);
    const auto applied = registrar.apply(ports::IntegrationKind::DesktopEntry, paths->exe_dir() / "reboot");
    REQUIRE_FALSE(applied);
    CHECK(applied.error().is(kIntegrationNeedsUserInstall));
    CHECK(applied.error().kind == ErrorKind::Unsupported);
    LinuxIntegrationRegistrar overridden(*paths, "0123456789abcdef", NativePath{"/srv/reboot"});
    const auto agent = overridden.apply(ports::IntegrationKind::EngineAgent, paths->exe_dir() / "reboot-engine");
    REQUIRE_FALSE(agent);
    CHECK(agent.error().is(kEngineAgentDefaultRootOnly));
}

TEST_CASE("LinuxResolver passes the resolver suite", "[linux_conformance]") {
    Scratch scratch;
    LinuxResolver resolver;
    require_passed(testing::run_resolver_conformance(resolver, scratch.env()));
}

TEST_CASE("a resolver destroyed with lookups queued answers each of them once, before it is gone", "[linux_conformance]") {
    auto calls = std::make_shared<std::atomic<int>>(0);
    {
        LinuxResolver resolver;
        for (int i = 0; i < 32; ++i)
            resolver.resolve("localhost", {}, [calls](Result<std::vector<IpAddress>>) { ++*calls; });
    }
    // Lookups still inside getaddrinfo were answered as cancelled too; none answers later.
    CHECK(calls->load() == 32);
    std::this_thread::sleep_for(std::chrono::milliseconds{200});
    CHECK(calls->load() == 32);
}

TEST_CASE("SockDiagPortInspector passes the port inspector suite", "[linux_conformance]") {
    SockDiagPortInspector inspector;
    const std::unique_ptr<testing::IPortBinder> binder = testing::make_socket_port_binder();
    require_passed(testing::run_port_inspector_conformance(inspector, *binder));
}

TEST_CASE("SockDiagPeerInspector names the uid of a loopback peer", "[linux_conformance]") {
    SockDiagPeerInspector inspector;
    const std::unique_ptr<testing::IPortBinder> binder = testing::make_socket_port_binder();
    require_passed(
        testing::run_loopback_peer_inspector_conformance(inspector, *binder, static_cast<u32>(::geteuid())));
    const auto pair = binder->connect_loopback();
    REQUIRE(pair);
    const Endpoint nowhere{pair->first.address, Port{1}};
    const auto missing = inspector.peer_uid(pair->first, nowhere);
    REQUIRE(missing);
    CHECK_FALSE(missing->has_value());
}

TEST_CASE("XdgShell refuses anything but https and trashes into the user's trash", "[linux_conformance]") {
    Scratch scratch;
    LinuxFileSystem fs;
    XdgShell shell;
    const testing::ConformanceReport report = testing::run_shell_launcher_conformance(shell, fs, scratch.env());
    // Without GIO, or on a volume without a trash, trash fails rather than deleting.
    require_passed_except(report, {"trash", "trash moves the file away"});
    const auto refused = shell.open_url("http://example.invalid/");
    REQUIRE_FALSE(refused);
    CHECK(refused.error().is(kUrlNotHttps));
}

TEST_CASE("the log file system appends to 0600 files and lists regular files only", "[linux_conformance]") {
    Scratch scratch;
    LinuxLogFileSystem logs;
    const NativePath dir = scratch.dir.path() / "logs" / "nested";
    REQUIRE(logs.create_directories(dir));
    Result<ports::LogFile> file = logs.open_append(dir / "engine.log");
    REQUIRE(file);
    REQUIRE(file->append(bytes_of("one\n")));
    REQUIRE(file->flush());
    file->close();
    Result<ports::LogFile> again = logs.open_append(dir / "engine.log");
    REQUIRE(again);
    CHECK(again->size() == 4);
    REQUIRE(again->append(bytes_of("two\n")));
    again->close();
    struct stat info {};
    REQUIRE(::stat((dir / "engine.log").c_str(), &info) == 0);
    CHECK((info.st_mode & 0777) == 0600);

    fs::create_directory(dir / "subdir");
    fs::create_symlink(dir / "engine.log", dir / "link.log");
    const auto listed = logs.list(dir);
    REQUIRE(listed);
    REQUIRE(listed->size() == 1);
    CHECK(listed->front().path == dir / "engine.log");
    CHECK(listed->front().size == 8);
    CHECK_FALSE(logs.open_append(dir / "link.log"));
    REQUIRE(logs.remove(dir / "engine.log"));
    CHECK_FALSE(logs.remove(dir / "engine.log"));
}

TEST_CASE("make_platform composes every engine port Linux provides", "[linux_conformance]") {
    Scratch scratch;
    ports::PlatformOptions options;
    options.data_root_override = scratch.dir.path() / "data";
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
    const auto security = services->security->probe();
    REQUIRE(security);
    CHECK_FALSE(security->has_value());
    require_passed(testing::run_random_conformance(*services->random));
}
