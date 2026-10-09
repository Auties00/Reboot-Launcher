#include "unistd.hpp"

#include <catch2/catch_test_macros.hpp>

#include <fcntl.h>
#include <signal.h>
#include <sys/file.h>
#include <sys/socket.h>
#include <sys/wait.h>

#include <array>
#include <cerrno>
#include <filesystem>
#include <memory>
#include <string>
#include <utility>

#include "darwin_peer_credentials.hpp"
#include "darwin_user_temp_dir.hpp"
#include "launchctl_run.hpp"
#include "reboot/foundation/paths.hpp"
#include "reboot/foundation/random.hpp"
#include "reboot/os_macos/ipc/mac_caller_context.hpp"
#include "reboot/os_macos/ipc/mac_client_paths.hpp"
#include "reboot/os_macos/ipc/sm_app_service_engine_starter.hpp"
#include "reboot/os_macos/ipc/unix_socket_connector.hpp"
#include "reboot/os_macos/ipc/unix_socket_listener.hpp"
#include "reboot/posix/unique_fd.hpp"
#include "reboot/testing/port_conformance.hpp"
#include "reboot/testing/scratch_dir.hpp"
#include "reboot/testing/wall_clock_waiter.hpp"
#include "spawn_lock.hpp"

namespace {

using namespace reboot;
using namespace reboot::os_macos::ipc;

[[nodiscard]] NativePath user_temp_dir() {
    Result<NativePath> directory = darwin_user_temp_dir();
    REQUIRE(directory);
    return std::move(*directory);
}

[[nodiscard]] std::string fresh_endpoint(IRandom& random) {
    return ports::endpoint_name(ports::PeerIdentity{}, random_token_hex(random, 8));
}

[[nodiscard]] testing::ScratchDir scratch_dir(IRandom& random) {
    auto scratch = testing::ScratchDir::create(random, "reboot-macos-ipc-conformance");
    REQUIRE(scratch);
    return std::move(*scratch);
}

[[nodiscard]] ports::CallerContext own_caller_context() {
    Result<MacCallerContext> probe = MacCallerContext::detect();
    REQUIRE(probe);
    return probe->capture();
}

// Restores the SIGCHLD disposition it replaced.
class SigchldIgnored {
public:
    SigchldIgnored() {
        struct sigaction ignore {};
        ignore.sa_handler = SIG_IGN;
        REQUIRE(::sigaction(SIGCHLD, &ignore, &previous_) == 0);
    }
    ~SigchldIgnored() { ::sigaction(SIGCHLD, &previous_, nullptr); }
    SigchldIgnored(const SigchldIgnored&) = delete;
    SigchldIgnored& operator=(const SigchldIgnored&) = delete;

private:
    struct sigaction previous_ {};
};

}  // namespace

TEST_CASE("the AF_UNIX adapters pass the IPC port suite", "[ipc]") {
    OsRandom random;
    testing::WallClockWaiter waiter;
    testing::ScratchDir scratch = scratch_dir(random);
    const NativePath temp = user_temp_dir();

    testing::IpcConformanceSubject subject;
    subject.make_listener = [temp] { return std::make_unique<UnixSocketListener>(temp); };
    subject.connector = std::make_unique<UnixSocketConnector>(temp);
    subject.make_endpoint = [&random] { return fresh_endpoint(random); };
    subject.self_user_id = std::to_string(::geteuid());
    const auto report = testing::run_ipc_conformance(std::move(subject), {waiter, scratch.path()});
    INFO(report.describe());
    CHECK(report.passed());
}

TEST_CASE("a socket outside the per-user temp directory is untrusted at both ends", "[ipc]") {
    const NativePath temp = user_temp_dir();
    UnixSocketListener listener{temp};
    const auto listened = listener.listen("/tmp/reboot-launcher/0123456789abcdef.sock",
                                          [](std::unique_ptr<ports::IByteStream>) {});
    REQUIRE_FALSE(listened);
    CHECK(listened.error().domain == ErrorDomain::Ipc);

    UnixSocketConnector connector{temp};
    const auto connected = connector.connect("/tmp/reboot-launcher/0123456789abcdef.sock", std::chrono::milliseconds{100});
    REQUIRE_FALSE(connected);
    CHECK(connected.error().domain == ErrorDomain::Ipc);
}

TEST_CASE("the peer of a socket reads as this process", "[ipc]") {
    std::array<int, 2> pair{-1, -1};
    REQUIRE(::socketpair(AF_UNIX, SOCK_STREAM, 0, pair.data()) == 0);
    const posix::UniqueFd first{pair[0]};
    const posix::UniqueFd second{pair[1]};
    const Result<posix::PeerCredentials> peer = read_darwin_peer(first.get());
    REQUIRE(peer);
    CHECK(peer->uid == static_cast<u32>(::geteuid()));
    CHECK(peer->pid == static_cast<u32>(::getpid()));
}

TEST_CASE("the caller context is stable and names an audit session", "[ipc]") {
    Result<MacCallerContext> probe = MacCallerContext::detect();
    REQUIRE(probe);
    const auto report = testing::run_caller_context_conformance(*probe, static_cast<u32>(::getpid()));
    INFO(report.describe());
    CHECK(report.passed());
    CHECK_FALSE(probe->capture().os_session.empty());
    CHECK(probe->capture().display_env.empty());
}

TEST_CASE("the client paths pass the platform paths suite", "[ipc]") {
    Result<MacClientPaths> paths = MacClientPaths::detect();
    REQUIRE(paths);
    const auto report = testing::run_platform_paths_conformance(*paths);
    INFO(report.describe());
    CHECK(report.passed());
    CHECK(paths->ipc_runtime_base() == user_temp_dir());
    // This runner is a plain executable, never inside an app bundle.
    CHECK(paths->install_kind() == ports::InstallKind::Dev);
    CHECK_FALSE(paths->velopack_package_dir());
}

TEST_CASE("launchctl's exit status comes back, even with SIGCHLD ignored", "[launchctl]") {
    const std::array<std::string, 1> version{"version"};
    {
        const Result<LaunchctlRun> run = run_launchctl(version, std::chrono::seconds{10});
        REQUIRE(run);
        CHECK(run->end == LaunchctlRun::End::Exited);
        CHECK(run->code == 0);
    }
    {
        const SigchldIgnored ignored;
        const Result<LaunchctlRun> run = run_launchctl(version, std::chrono::seconds{10});
        REQUIRE(run);
        CHECK(run->end == LaunchctlRun::End::Exited);
        CHECK(run->code == 0);
    }
}

TEST_CASE("a launchctl past its deadline is killed and reaped", "[launchctl]") {
    const std::array<std::string, 1> version{"version"};
    // A zero deadline passes before the resumed child can even reach main.
    const Result<LaunchctlRun> run = run_launchctl(version, std::chrono::milliseconds{0});
    REQUIRE(run);
    CHECK(run->end == LaunchctlRun::End::TimedOut);
    int status = 0;
    const pid_t waited = ::waitpid(-1, &status, WNOHANG);
    const int error = errno;
    CHECK(waited == -1);
    CHECK(error == ECHILD);
}

TEST_CASE("the spawn lock excludes every other holder until it is released", "[launchctl]") {
    OsRandom random;
    testing::ScratchDir scratch = scratch_dir(random);
    const NativePath state = scratch.path() / "root" / "state";
    REQUIRE(create_private_dirs(state));
    const NativePath lock_path = state / "spawn.lock";
    {
        const Result<posix::UniqueFd> held = lock_exclusive(lock_path);
        REQUIRE(held);
        const posix::UniqueFd other{::open(lock_path.c_str(), O_RDWR | O_CLOEXEC)};
        REQUIRE(other.valid());
        const int locked = ::flock(other.get(), LOCK_EX | LOCK_NB);
        const int error = errno;
        CHECK(locked != 0);
        CHECK(error == EWOULDBLOCK);
    }
    const posix::UniqueFd other{::open(lock_path.c_str(), O_RDWR | O_CLOEXEC)};
    REQUIRE(other.valid());
    CHECK(::flock(other.get(), LOCK_EX | LOCK_NB) == 0);
}

TEST_CASE("an agent this machine never registered awaits the user", "[launchctl]") {
    const ports::CallerContext caller = own_caller_context();
    OsRandom random;
    testing::ScratchDir scratch = scratch_dir(random);
    const DataRoot root{.root = scratch.path() / "root", .overridden = false};
    SmAppServiceEngineStarter starter{static_cast<u32>(::geteuid()), caller};

    const Result<ports::StartResult> started = starter.ensure_started(NativePath{"/nonexistent/reboot-engine"}, root);
    REQUIRE(started);
    if (caller.elevated) {
        CHECK(*started == ports::StartResult::ElevatedRefused);
    } else if (!caller.interactive) {
        CHECK(*started == ports::StartResult::NoInteractiveSession);
    } else {
        // CI runners never registered the agent, so kickstart finds no such label in gui/<uid>.
        CHECK(*started == ports::StartResult::AwaitingUser);
        CHECK(std::filesystem::exists(root.root / "state" / "spawn.lock"));
    }
}

TEST_CASE("an overridden root is never started through the agent", "[launchctl]") {
    ports::CallerContext caller = own_caller_context();
    caller.elevated = false;
    caller.interactive = true;
    SmAppServiceEngineStarter starter{static_cast<u32>(::geteuid()), caller};
    const Result<ports::StartResult> started =
        starter.ensure_started(NativePath{"/nonexistent/reboot-engine"}, DataRoot{.root = "/nonexistent/root", .overridden = true});
    REQUIRE(started);
    CHECK(*started == ports::StartResult::CannotDetach);
}
