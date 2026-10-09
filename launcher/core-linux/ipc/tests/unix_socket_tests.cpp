#include <catch2/catch_message.hpp>
#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <format>
#include <memory>
#include <mutex>
#include <string>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>
#include <utility>
#include <vector>

#include "engine_socket_path.hpp"
#include "linux_ipc_test_support.hpp"
#include "linux_peer_credentials.hpp"
#include "messages.hpp"
#include "reboot/os_linux/ipc/unix_socket_connector.hpp"
#include "reboot/os_linux/ipc/unix_socket_listener.hpp"
#include "reboot/posix/unique_fd.hpp"
#include "reboot/testing/port_conformance.hpp"
#include "reboot/testing/wall_clock_waiter.hpp"

using namespace rb;
using namespace rb::os_linux::ipc;
using namespace rb::os_linux::ipc::test;
using namespace std::chrono_literals;

namespace {

[[nodiscard]] std::string endpoint_in(const NativePath& runtime_base, unsigned number) {
    return engine_socket_path(runtime_base, std::format("{:016x}", number)).string();
}

void make_private_dir(const NativePath& directory) {
    REQUIRE(::mkdir(directory.c_str(), 0700) == 0);
    REQUIRE(::chmod(directory.c_str(), 0700) == 0);
}

[[nodiscard]] posix::UniqueFd unix_stream_socket() {
    posix::UniqueFd fd{::socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0)};
    REQUIRE(fd.valid());
    return fd;
}

void bind_to(int fd, const NativePath& path) {
    sockaddr_un address{};
    address.sun_family = AF_UNIX;
    REQUIRE(path.native().size() < sizeof address.sun_path);
    std::memcpy(address.sun_path, path.c_str(), path.native().size() + 1);
    REQUIRE(::bind(fd, reinterpret_cast<const sockaddr*>(&address), sizeof address) == 0);
}

// What systemd passes for reboot-engine.socket: a listening socket bound to `path`.
[[nodiscard]] posix::UniqueFd systemd_socket(const NativePath& path) {
    posix::UniqueFd fd = unix_stream_socket();
    bind_to(fd.get(), path);
    REQUIRE(::chmod(path.c_str(), 0600) == 0);
    REQUIRE(::listen(fd.get(), 8) == 0);
    return fd;
}

// Puts `fd` at number 3, where sd_listen_fds(3) starts, and restores what was there.
class Fd3Slot {
public:
    explicit Fd3Slot(int fd) {
        struct stat info {};
        REQUIRE(::fstat(fd, &info) == 0);
        inode_ = info.st_ino;
        saved_ = ::fcntl(3, F_DUPFD_CLOEXEC, 10);
        REQUIRE(::dup2(fd, 3) == 3);
    }
    ~Fd3Slot() {
        // The listener owns and closes fd 3 once it accepted it.
        struct stat info {};
        if (::fstat(3, &info) == 0 && info.st_ino == inode_) ::close(3);
        if (saved_ >= 0) {
            ::dup2(saved_, 3);
            ::close(saved_);
        }
    }
    Fd3Slot(const Fd3Slot&) = delete;
    Fd3Slot& operator=(const Fd3Slot&) = delete;

private:
    ino_t inode_ = 0;
    int saved_ = -1;
};

// Streams a listener handed over, kept alive.
class Accepted {
public:
    UniqueFunction<void(std::unique_ptr<ports::IByteStream>)> sink() {
        return [this](std::unique_ptr<ports::IByteStream> stream) {
            const std::lock_guard lock{mutex_};
            streams_.push_back(std::move(stream));
        };
    }
    [[nodiscard]] std::size_t count() const {
        const std::lock_guard lock{mutex_};
        return streams_.size();
    }

private:
    mutable std::mutex mutex_;
    std::vector<std::unique_ptr<ports::IByteStream>> streams_;
};

void check_untrusted(const Diagnostic& error, const MessageId& cause) {
    CHECK(error.is(posix::kEndpointUntrusted));
    REQUIRE(error.causes.size() == 1);
    CHECK(error.causes.front().is(cause));
}

}  // namespace

TEST_CASE("the Linux listener and connector pass the IPC conformance suite", "[unix_socket]") {
    const EnvOverride listen_pid{"LISTEN_PID", std::nullopt};
    const auto scratch = make_private_scratch("linux-ipc");
    unsigned next = 0;
    testing::WallClockWaiter waiter;
    testing::IpcConformanceSubject subject{
        .make_listener = [&]() -> std::unique_ptr<ports::IIpcListener> {
            return std::make_unique<UnixSocketListener>(scratch.path());
        },
        .connector = std::make_unique<UnixSocketConnector>(scratch.path()),
        .make_endpoint = [&] { return endpoint_in(scratch.path(), ++next); },
        .self_user_id = std::to_string(own_uid()),
    };
    const testing::ConformanceReport report =
        testing::run_ipc_conformance(std::move(subject), {.waiter = waiter, .scratch = scratch.path()});
    INFO(report.describe());
    CHECK(report.passed());
}

TEST_CASE("both ends refuse a path that is not <runtime_base>/reboot-launcher/<hash16>.sock", "[unix_socket]") {
    const auto scratch = make_private_scratch("linux-ipc");
    const NativePath base = scratch.path();
    UnixSocketListener listener{base};
    UnixSocketConnector connector{base};
    for (const std::string& name : {(base / "reboot-launcher" / "engine.sock").string(),
                                    (base / "elsewhere" / "0123456789abcdef.sock").string(),
                                    (base / "reboot-launcher" / "0123456789ABCDEF.sock").string(),
                                    std::string("/tmp/reboot-launcher/0123456789abcdef.sock")}) {
        INFO(name);
        const auto listened = listener.listen(name, [](std::unique_ptr<ports::IByteStream>) {});
        REQUIRE_FALSE(listened);
        check_untrusted(listened.error(), kEndpointOutsideRuntimeDir);
        const auto connected = connector.connect(name, 100ms);
        REQUIRE_FALSE(connected);
        check_untrusted(connected.error(), kEndpointOutsideRuntimeDir);
    }
    CHECK_FALSE(path_exists(base / "reboot-launcher"));
}

TEST_CASE("the listener creates a missing runtime base 0700 and serves the socket 0600", "[unix_socket]") {
    const EnvOverride listen_pid{"LISTEN_PID", std::nullopt};
    const auto scratch = make_private_scratch("linux-ipc");
    const NativePath base = scratch.path() / "fallback";
    const std::string endpoint = endpoint_in(base, 1);
    Accepted accepted;
    UnixSocketListener listener{base};
    {
        const mode_t previous = ::umask(0);
        const auto listened = listener.listen(endpoint, accepted.sink());
        ::umask(previous);
        REQUIRE(listened);
    }
    CHECK(mode_of(base) == 0700U);
    CHECK(mode_of(base / "reboot-launcher") == 0700U);
    CHECK(mode_of(NativePath{endpoint}) == 0600U);

    UnixSocketConnector connector{base};
    auto stream = connector.connect(endpoint, 5s);
    REQUIRE(stream);
    CHECK((*stream)->peer().user_id == std::to_string(own_uid()));
    CHECK((*stream)->peer().pid == static_cast<u32>(::getpid()));
    CHECK(wait_for([&] { return accepted.count() == 1; }));

    listener.close();
    CHECK_FALSE(path_exists(NativePath{endpoint}));
}

TEST_CASE("a runtime base that is not a private directory of ours is untrusted", "[unix_socket]") {
    const EnvOverride listen_pid{"LISTEN_PID", std::nullopt};
    const auto scratch = make_private_scratch("linux-ipc");

    SECTION("group- or world-accessible") {
        const NativePath base = scratch.path() / "open";
        REQUIRE(::mkdir(base.c_str(), 0700) == 0);
        REQUIRE(::chmod(base.c_str(), 0755) == 0);
        const auto listened = UnixSocketListener{base}.listen(endpoint_in(base, 1), [](auto) {});
        REQUIRE_FALSE(listened);
        check_untrusted(listened.error(), posix::kDirectoryNotPrivate);
        const auto connected = UnixSocketConnector{base}.connect(endpoint_in(base, 1), 100ms);
        REQUIRE_FALSE(connected);
        check_untrusted(connected.error(), posix::kDirectoryNotPrivate);
    }

    SECTION("a symlink to a private directory") {
        const NativePath real = scratch.path() / "real";
        make_private_dir(real);
        const NativePath base = scratch.path() / "link";
        REQUIRE(::symlink(real.c_str(), base.c_str()) == 0);
        const auto listened = UnixSocketListener{base}.listen(endpoint_in(base, 1), [](auto) {});
        REQUIRE_FALSE(listened);
        check_untrusted(listened.error(), posix::kNotADirectory);
        const auto connected = UnixSocketConnector{base}.connect(endpoint_in(base, 1), 100ms);
        REQUIRE_FALSE(connected);
        check_untrusted(connected.error(), posix::kNotADirectory);
        CHECK_FALSE(path_exists(real / "reboot-launcher"));
    }
}

TEST_CASE("connect before any runtime base exists is retryable engine_not_listening", "[unix_socket]") {
    const auto scratch = make_private_scratch("linux-ipc");
    const NativePath base = scratch.path() / "missing";
    const auto connected = UnixSocketConnector{base}.connect(endpoint_in(base, 1), 100ms);
    REQUIRE_FALSE(connected);
    CHECK(connected.error().is(posix::kEngineNotListening));
    CHECK(connected.error().kind == ErrorKind::EngineUnavailable);
    CHECK(connected.error().retryable);
    CHECK_FALSE(path_exists(base));
}

TEST_CASE("an inherited socket bound to the endpoint is served and never unlinked", "[socket_activation]") {
    const auto scratch = make_private_scratch("linux-ipc");
    const NativePath base = scratch.path();
    make_private_dir(base / "reboot-launcher");
    const std::string endpoint = endpoint_in(base, 1);
    std::optional<Fd3Slot> slot;
    {
        const posix::UniqueFd socket = systemd_socket(NativePath{endpoint});
        slot.emplace(socket.get());
    }
    const EnvOverride listen_pid{"LISTEN_PID", std::to_string(::getpid())};
    const EnvOverride listen_fds{"LISTEN_FDS", "1"};

    Accepted accepted;
    UnixSocketListener listener{base};
    REQUIRE(listener.listen(endpoint, accepted.sink()));
    CHECK((::fcntl(3, F_GETFD) & FD_CLOEXEC) != 0);
    auto stream = UnixSocketConnector{base}.connect(endpoint, 5s);
    REQUIRE(stream);
    CHECK(wait_for([&] { return accepted.count() == 1; }));

    listener.close();
    CHECK(path_exists(NativePath{endpoint}));
    CHECK(std::getenv("LISTEN_PID") != nullptr);
    CHECK(std::getenv("LISTEN_FDS") != nullptr);
}

TEST_CASE("an inherited socket bound to another path is a mismatch", "[socket_activation]") {
    const auto scratch = make_private_scratch("linux-ipc");
    const NativePath base = scratch.path();
    make_private_dir(base / "reboot-launcher");
    const posix::UniqueFd socket = systemd_socket(NativePath{endpoint_in(base, 2)});
    const Fd3Slot slot{socket.get()};
    const EnvOverride listen_pid{"LISTEN_PID", std::to_string(::getpid())};
    const EnvOverride listen_fds{"LISTEN_FDS", "1"};

    const auto listened = UnixSocketListener{base}.listen(endpoint_in(base, 1), [](auto) {});
    REQUIRE_FALSE(listened);
    CHECK(listened.error().is(kInheritedSocketMismatch));
    CHECK_FALSE(path_exists(NativePath{endpoint_in(base, 1)}));
}

TEST_CASE("anything but one listening AF_UNIX stream socket at fd 3 is invalid", "[socket_activation]") {
    const auto scratch = make_private_scratch("linux-ipc");
    const NativePath base = scratch.path();
    make_private_dir(base / "reboot-launcher");
    const std::string endpoint = endpoint_in(base, 1);
    const EnvOverride listen_pid{"LISTEN_PID", std::to_string(::getpid())};

    SECTION("two sockets") {
        const posix::UniqueFd socket = systemd_socket(NativePath{endpoint});
        const Fd3Slot slot{socket.get()};
        const EnvOverride listen_fds{"LISTEN_FDS", "2"};
        const auto listened = UnixSocketListener{base}.listen(endpoint, [](auto) {});
        REQUIRE_FALSE(listened);
        CHECK(listened.error().is(kInheritedSocketInvalid));
    }

    SECTION("a socket that is bound but not listening") {
        const posix::UniqueFd socket = unix_stream_socket();
        bind_to(socket.get(), NativePath{endpoint});
        const Fd3Slot slot{socket.get()};
        const EnvOverride listen_fds{"LISTEN_FDS", "1"};
        const auto listened = UnixSocketListener{base}.listen(endpoint, [](auto) {});
        REQUIRE_FALSE(listened);
        CHECK(listened.error().is(kInheritedSocketInvalid));
    }

    SECTION("a pipe") {
        std::array<int, 2> ends{-1, -1};
        REQUIRE(::pipe2(ends.data(), O_CLOEXEC) == 0);
        const posix::UniqueFd read_end{ends[0]};
        const posix::UniqueFd write_end{ends[1]};
        const Fd3Slot slot{read_end.get()};
        const EnvOverride listen_fds{"LISTEN_FDS", "1"};
        const auto listened = UnixSocketListener{base}.listen(endpoint, [](auto) {});
        REQUIRE_FALSE(listened);
        CHECK(listened.error().is(kInheritedSocketInvalid));
    }
}

TEST_CASE("LISTEN_* variables of another process leave the listener to bind its own", "[socket_activation]") {
    const auto scratch = make_private_scratch("linux-ipc");
    const NativePath base = scratch.path();
    const std::string endpoint = endpoint_in(base, 1);
    const EnvOverride listen_pid{"LISTEN_PID", std::to_string(::getpid() + 1)};
    const EnvOverride listen_fds{"LISTEN_FDS", "1"};

    UnixSocketListener listener{base};
    REQUIRE(listener.listen(endpoint, [](auto) {}));
    CHECK(path_exists(NativePath{endpoint}));
    listener.close();
    CHECK_FALSE(path_exists(NativePath{endpoint}));
}

TEST_CASE("SO_PEERCRED names this process at the other end of a socket pair", "[unix_socket]") {
    std::array<int, 2> pair{-1, -1};
    REQUIRE(::socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, pair.data()) == 0);
    const posix::UniqueFd first{pair[0]};
    const posix::UniqueFd second{pair[1]};
    const auto peer = read_linux_peer(first.get());
    REQUIRE(peer);
    CHECK(peer->uid == own_uid());
    CHECK(peer->pid == static_cast<u32>(::getpid()));

    const auto closed = read_linux_peer(-1);
    REQUIRE_FALSE(closed);
}
