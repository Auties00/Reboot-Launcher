#include <atomic>
#include <catch2/catch_test_macros.hpp>
#include <cerrno>
#include <chrono>
#include <fcntl.h>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <sys/socket.h>
#include <sys/stat.h>
#include <utility>
#include <vector>

#include "messages.hpp"
#include "posix_test_support.hpp"
#include "reboot/foundation/diag.hpp"
#include "reboot/posix/peer_credential_check.hpp"
#include "reboot/posix/unique_fd.hpp"
#include "reboot/posix/unix_socket_connector_base.hpp"
#include "reboot/posix/unix_socket_listener_base.hpp"
#include "reboot/testing/port_conformance.hpp"
#include "reboot/testing/wall_clock_waiter.hpp"
#include "socket_fds.hpp"
#include "unistd.hpp"
#include "unix_endpoint_checks.hpp"

using namespace rb;
using namespace rb::posix;
using namespace rb::posix::test;
using namespace std::chrono_literals;

namespace {

[[nodiscard]] PeerCredentialCheck own_check(PeerCredentialCheck::Reader reader = [](int fd) { return read_peer(fd); }) {
    return PeerCredentialCheck{std::move(reader), own_uid()};
}

class TestListener final : public UnixSocketListenerBase {
public:
    explicit TestListener(PeerCredentialCheck check = own_check()) : UnixSocketListenerBase(std::move(check)) {}
};

class TestConnector final : public UnixSocketConnectorBase {
public:
    explicit TestConnector(PeerCredentialCheck check = own_check()) : UnixSocketConnectorBase(std::move(check)) {}
};

// Stands in for systemd's LISTEN_FDS socket.
class InheritingListener final : public UnixSocketListenerBase {
public:
    explicit InheritingListener(UniqueFd socket) : UnixSocketListenerBase(own_check()), socket_(std::move(socket)) {}

    std::optional<NativePath> asked_for;

protected:
    Result<std::optional<UniqueFd>> take_inherited_socket(const NativePath& socket_path) override {
        asked_for = socket_path;
        return std::optional<UniqueFd>{std::move(socket_)};
    }

private:
    UniqueFd socket_;
};

// Streams a listener handed over, kept alive with what they received.
class Accepted {
public:
    UniqueFunction<void(std::unique_ptr<ports::IByteStream>)> sink() {
        return [this](std::unique_ptr<ports::IByteStream> stream) {
            auto log = std::make_shared<StreamLog>();
            stream->on_read([log](std::span<const u8> bytes) { log->append(bytes); });
            stream->on_close([log] { log->closed(); });
            const std::lock_guard lock{mutex_};
            entries_.push_back({std::move(stream), std::move(log)});
        };
    }
    [[nodiscard]] std::size_t count() const {
        const std::lock_guard lock{mutex_};
        return entries_.size();
    }
    [[nodiscard]] std::shared_ptr<StreamLog> log(std::size_t index) const {
        const std::lock_guard lock{mutex_};
        return entries_.at(index).log;
    }
    [[nodiscard]] ports::IByteStream& stream(std::size_t index) const {
        const std::lock_guard lock{mutex_};
        return *entries_.at(index).stream;
    }
    // Joins the streams' threads before the logs go away.
    void clear() {
        const std::lock_guard lock{mutex_};
        entries_.clear();
    }

private:
    struct Entry {
        std::unique_ptr<ports::IByteStream> stream;
        std::shared_ptr<StreamLog> log;
    };
    mutable std::mutex mutex_;
    std::vector<Entry> entries_;
};

[[nodiscard]] std::shared_ptr<StreamLog> watch(ports::IByteStream& stream) {
    auto log = std::make_shared<StreamLog>();
    stream.on_read([log](std::span<const u8> bytes) { log->append(bytes); });
    stream.on_close([log] { log->closed(); });
    return log;
}

[[nodiscard]] std::string endpoint_in(const NativePath& directory) { return (directory / "engine.sock").string(); }

[[nodiscard]] NativePath make_private_dir(const NativePath& directory) {
    REQUIRE(::mkdir(directory.c_str(), 0700) == 0);
    REQUIRE(::chmod(directory.c_str(), 0700) == 0);
    return directory;
}

// A socket file nobody listens on, as a crashed engine leaves behind.
void leave_stale_socket(const NativePath& path) {
    auto socket = make_unix_stream_socket();
    REQUIRE(socket.has_value());
    REQUIRE(bind_unix(socket->get(), path) == 0);
}

void expect_not_listening(const Result<std::unique_ptr<ports::IByteStream>>& result) {
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().is(kEngineNotListening));
    CHECK(result.error().kind == ErrorKind::EngineUnavailable);
    CHECK(result.error().retryable);
}

}  // namespace

TEST_CASE("the AF_UNIX listener and connector pass the IPC conformance suite") {
    const auto scratch = make_scratch("posix-ipc");
    int next = 0;
    testing::WallClockWaiter waiter;
    testing::IpcConformanceSubject subject{
        .make_listener = []() -> std::unique_ptr<ports::IIpcListener> { return std::make_unique<TestListener>(); },
        .connector = std::make_unique<TestConnector>(),
        .make_endpoint = [&] { return endpoint_in(scratch.path() / ("e" + std::to_string(++next))); },
        .self_user_id = std::to_string(own_uid()),
    };
    const testing::ConformanceReport report =
        testing::run_ipc_conformance(std::move(subject), {.waiter = waiter, .scratch = scratch.path()});
    INFO(report.describe());
    CHECK(report.passed());
}

TEST_CASE("listen creates a 0700 directory and a 0600 socket, and close unlinks it") {
    const auto scratch = make_scratch("posix-ipc");
    const NativePath directory = scratch.path() / "endpoint";
    const std::string endpoint = endpoint_in(directory);
    Accepted accepted;
    TestListener listener;
    {
        const UmaskGuard umask{0};
        REQUIRE(listener.listen(endpoint, accepted.sink()).has_value());
    }
    CHECK(mode_of(directory) == 0700U);
    CHECK(mode_of(endpoint) == 0600U);
    struct stat info {};
    REQUIRE(::lstat(endpoint.c_str(), &info) == 0);
    CHECK(S_ISSOCK(info.st_mode));

    TestConnector connector;
    auto client = connector.connect(endpoint, 5s);
    REQUIRE(client.has_value());
    CHECK((*client)->peer().user_id == std::to_string(own_uid()));
    REQUIRE(wait_for([&] { return accepted.count() == 1; }));
    CHECK(accepted.stream(0).peer().user_id == std::to_string(own_uid()));
    const u32 client_pid = accepted.stream(0).peer().pid;
    CHECK((client_pid == static_cast<u32>(::getpid()) || client_pid == 0));

    listener.close();
    CHECK_FALSE(path_exists(endpoint));
    listener.close();
    expect_not_listening(connector.connect(endpoint, 1s));
    // An accepted stream outlives its listener.
    (*client)->write(as_bytes("still here"));
    REQUIRE(wait_for([&] { return accepted.log(0)->received() == "still here"; }));
    client->reset();
    accepted.clear();
}

TEST_CASE("a closed listener can listen again") {
    const auto scratch = make_scratch("posix-ipc");
    const std::string endpoint = endpoint_in(scratch.path() / "endpoint");
    Accepted accepted;
    TestListener listener;
    REQUIRE(listener.listen(endpoint, accepted.sink()).has_value());
    CHECK_FALSE(listener.listen(endpoint, accepted.sink()).has_value());
    listener.close();
    REQUIRE(listener.listen(endpoint, accepted.sink()).has_value());

    TestConnector connector;
    auto client = connector.connect(endpoint, 5s);
    REQUIRE(client.has_value());
    REQUIRE(wait_for([&] { return accepted.count() == 1; }));
    client->reset();
    accepted.clear();
}

TEST_CASE("listen refuses a live endpoint but replaces a stale socket or file") {
    const auto scratch = make_scratch("posix-ipc");
    const NativePath directory = make_private_dir(scratch.path() / "endpoint");
    const std::string endpoint = endpoint_in(directory);

    SECTION("live") {
        Accepted first_accepted;
        TestListener first;
        REQUIRE(first.listen(endpoint, first_accepted.sink()).has_value());
        TestListener second;
        const auto squatted = second.listen(endpoint, [](std::unique_ptr<ports::IByteStream>) {});
        REQUIRE_FALSE(squatted.has_value());
        CHECK(squatted.error().is(kEndpointInUse));
        CHECK(squatted.error().kind == ErrorKind::Conflict);
        // The refused listener leaves the live socket alone.
        second.close();
        CHECK(path_exists(endpoint));
        TestConnector connector;
        auto client = connector.connect(endpoint, 5s);
        CHECK(client.has_value());
        first.close();
        first_accepted.clear();
    }
    SECTION("stale socket") {
        leave_stale_socket(endpoint);
        TestConnector connector;
        expect_not_listening(connector.connect(endpoint, 1s));
        Accepted accepted;
        TestListener listener;
        REQUIRE(listener.listen(endpoint, accepted.sink()).has_value());
        auto client = connector.connect(endpoint, 5s);
        CHECK(client.has_value());
        listener.close();
        accepted.clear();
    }
    SECTION("regular file") {
        const UniqueFd file{::open(endpoint.c_str(), O_WRONLY | O_CREAT | O_CLOEXEC, 0600)};
        REQUIRE(file.valid());
        Accepted accepted;
        TestListener listener;
        REQUIRE(listener.listen(endpoint, accepted.sink()).has_value());
        TestConnector connector;
        CHECK(connector.connect(endpoint, 5s).has_value());
        listener.close();
        accepted.clear();
    }
}

TEST_CASE("both ends refuse a socket directory that is not private") {
    const auto scratch = make_scratch("posix-ipc");
    TestListener listener;
    TestConnector connector;

    SECTION("loose mode") {
        const NativePath directory = scratch.path() / "loose";
        REQUIRE(::mkdir(directory.c_str(), 0755) == 0);
        REQUIRE(::chmod(directory.c_str(), 0755) == 0);
        for (const auto& result : {listener.listen(endpoint_in(directory), [](std::unique_ptr<ports::IByteStream>) {}),
                                   connector.connect(endpoint_in(directory), 1s).transform([](auto&&) {})}) {
            REQUIRE_FALSE(result.has_value());
            CHECK(result.error().is(kEndpointUntrusted));
            REQUIRE(result.error().causes.size() == 1);
            const Diagnostic& cause = result.error().causes.front();
            CHECK(cause.is(kDirectoryNotPrivate));
            REQUIRE(cause.find_arg("mode") != nullptr);
            CHECK(*cause.find_arg("mode") == Arg{std::string("0755")});
        }
    }
    SECTION("a link to a private directory") {
        const NativePath target = make_private_dir(scratch.path() / "target");
        const NativePath link = scratch.path() / "link";
        REQUIRE(::symlink(target.c_str(), link.c_str()) == 0);
        for (const auto& result : {listener.listen(endpoint_in(link), [](std::unique_ptr<ports::IByteStream>) {}),
                                   connector.connect(endpoint_in(link), 1s).transform([](auto&&) {})}) {
            REQUIRE_FALSE(result.has_value());
            CHECK(result.error().is(kEndpointUntrusted));
            REQUIRE(result.error().causes.size() == 1);
            CHECK(result.error().causes.front().is(kNotADirectory));
        }
        CHECK_FALSE(path_exists(target / "engine.sock"));
    }
    SECTION("another owner") {
        const NativePath directory = make_private_dir(scratch.path() / "private");
        TestConnector stranger{PeerCredentialCheck{[](int fd) { return read_peer(fd); }, own_uid() + 1}};
        const auto result = stranger.connect(endpoint_in(directory), 1s);
        REQUIRE_FALSE(result.has_value());
        CHECK(result.error().is(kEndpointUntrusted));
        REQUIRE(result.error().causes.size() == 1);
        CHECK(result.error().causes.front().is(kDirectoryNotPrivate));
    }
}

TEST_CASE("both ends refuse a socket path longer than sun_path") {
    const auto scratch = make_scratch("posix-ipc");
    const NativePath directory = make_private_dir(scratch.path() / "d");
    const std::string endpoint = (directory / std::string(sun_path_capacity(), 's')).string();

    TestListener listener;
    const auto listened = listener.listen(endpoint, [](std::unique_ptr<ports::IByteStream>) {});
    REQUIRE_FALSE(listened.has_value());
    CHECK(listened.error().is(kSocketPathTooLong));
    TestConnector connector;
    const auto connected = connector.connect(endpoint, 1s);
    REQUIRE_FALSE(connected.has_value());
    CHECK(connected.error().is(kSocketPathTooLong));
}

TEST_CASE("connect reports a missing directory, socket or listener as engine_not_listening") {
    const auto scratch = make_scratch("posix-ipc");
    TestConnector connector;
    expect_not_listening(connector.connect(endpoint_in(scratch.path() / "missing"), 1s));

    const NativePath directory = make_private_dir(scratch.path() / "endpoint");
    expect_not_listening(connector.connect(endpoint_in(directory), 1s));

    leave_stale_socket(endpoint_in(directory));
    expect_not_listening(connector.connect(endpoint_in(directory), 1s));
}

TEST_CASE("the connector refuses a listener of another uid before writing anything") {
    const auto scratch = make_scratch("posix-ipc");
    const std::string endpoint = endpoint_in(scratch.path() / "endpoint");
    Accepted accepted;
    TestListener listener;
    REQUIRE(listener.listen(endpoint, accepted.sink()).has_value());

    TestConnector connector{own_check([](int fd) { return read_peer_as_stranger(fd); })};
    const auto result = connector.connect(endpoint, 5s);
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().is(kEndpointUntrusted));
    REQUIRE(result.error().causes.size() == 1);
    const Diagnostic& cause = result.error().causes.front();
    CHECK(cause.is(kPeerOtherUser));
    REQUIRE(cause.find_arg("peer_uid") != nullptr);
    CHECK(*cause.find_arg("peer_uid") == Arg{u64{own_uid() + 1}});

    // The engine side got no byte: a stream that ended at once, or nothing where the peer left before
    // the accept (macOS). A later client's bytes show the listener has gone past the refused one.
    TestConnector later;
    auto client = later.connect(endpoint, 5s);
    REQUIRE(client.has_value());
    (*client)->write(as_bytes("later"));
    REQUIRE(wait_for([&] { return accepted.count() >= 1 && accepted.log(accepted.count() - 1)->received() == "later"; }));
    REQUIRE(accepted.count() <= 2);
    if (accepted.count() == 2) {
        CHECK(accepted.log(0)->received().empty());
        CHECK(wait_for([&] { return accepted.log(0)->closes() == 1; }));
    }
    client->reset();
    listener.close();
    accepted.clear();
}

TEST_CASE("the listener drops a peer of another uid before handing it over") {
    const auto scratch = make_scratch("posix-ipc");
    const std::string endpoint = endpoint_in(scratch.path() / "endpoint");
    Accepted accepted;
    TestListener listener{own_check([](int fd) { return read_peer_as_stranger(fd); })};
    REQUIRE(listener.listen(endpoint, accepted.sink()).has_value());

    TestConnector connector;
    auto client = connector.connect(endpoint, 5s);
    REQUIRE(client.has_value());
    const auto log = watch(**client);
    (*client)->write(as_bytes("hello"));
    REQUIRE(wait_for([&] { return log->closes() == 1; }));
    CHECK(accepted.count() == 0);
    client->reset();
    listener.close();
}

TEST_CASE("an inherited socket is used as passed and its path is left to the service manager") {
    const auto scratch = make_scratch("posix-ipc");
    const NativePath directory = make_private_dir(scratch.path() / "endpoint");
    const std::string endpoint = endpoint_in(directory);
    UniqueFd socket{::socket(AF_UNIX, SOCK_STREAM, 0)};
    REQUIRE(socket.valid());
    REQUIRE(bind_unix(socket.get(), NativePath{endpoint}) == 0);
    REQUIRE(::listen(socket.get(), 8) == 0);
    const int fd = socket.get();

    Accepted accepted;
    InheritingListener listener{std::move(socket)};
    REQUIRE(listener.listen(endpoint, accepted.sink()).has_value());
    REQUIRE(listener.asked_for.has_value());
    CHECK(*listener.asked_for == NativePath{endpoint});
    CHECK((::fcntl(fd, F_GETFD) & FD_CLOEXEC) != 0);
    CHECK((::fcntl(fd, F_GETFL) & O_NONBLOCK) != 0);

    TestConnector connector;
    auto client = connector.connect(endpoint, 5s);
    REQUIRE(client.has_value());
    REQUIRE(wait_for([&] { return accepted.count() == 1; }));

    listener.close();
    CHECK(path_exists(endpoint));
    client->reset();
    accepted.clear();
}

TEST_CASE("the listener hands over many clients connecting at once") {
    const auto scratch = make_scratch("posix-ipc");
    const std::string endpoint = endpoint_in(scratch.path() / "endpoint");
    Accepted accepted;
    TestListener listener;
    REQUIRE(listener.listen(endpoint, accepted.sink()).has_value());

    constexpr std::size_t kClients = 16;
    TestConnector connector;
    std::vector<std::unique_ptr<ports::IByteStream>> clients;
    for (std::size_t i = 0; i < kClients; ++i) {
        auto client = connector.connect(endpoint, 5s);
        REQUIRE(client.has_value());
        (*client)->write(as_bytes(std::to_string(i)));
        clients.push_back(std::move(*client));
    }
    REQUIRE(wait_for([&] { return accepted.count() == kClients; }));
    REQUIRE(wait_for([&] {
        for (std::size_t i = 0; i < kClients; ++i)
            if (accepted.log(i)->received().empty()) return false;
        return true;
    }));
    clients.clear();
    listener.close();
    accepted.clear();
}

TEST_CASE("sun_path_capacity is the platform's sockaddr_un limit") {
#if defined(__APPLE__)
    CHECK(sun_path_capacity() == 104);
#else
    CHECK(sun_path_capacity() == 108);
#endif
}

TEST_CASE("ensure_private_directory creates only the last component and checks what exists") {
    const auto scratch = make_scratch("posix-ipc");
    const auto orphan = ensure_private_directory(scratch.path() / "missing" / "endpoint", own_uid());
    REQUIRE_FALSE(orphan.has_value());
    CHECK(orphan.error().kind == ErrorKind::NotFound);
    CHECK_FALSE(path_exists(scratch.path() / "missing"));

    const NativePath directory = scratch.path() / "endpoint";
    REQUIRE(ensure_private_directory(directory, own_uid()).has_value());
    CHECK(mode_of(directory) == 0700U);
    CHECK(ensure_private_directory(directory, own_uid()).has_value());
    CHECK(check_private_directory(directory, own_uid()).has_value());

    const auto foreign = check_private_directory(directory, own_uid() + 1);
    REQUIRE_FALSE(foreign.has_value());
    REQUIRE(foreign.error().causes.size() == 1);
    const Diagnostic& cause = foreign.error().causes.front();
    CHECK(cause.is(kDirectoryNotPrivate));
    REQUIRE(cause.find_arg("owner_uid") != nullptr);
    CHECK(*cause.find_arg("owner_uid") == Arg{u64{own_uid()}});
    REQUIRE(cause.find_arg("mode") != nullptr);
    CHECK(*cause.find_arg("mode") == Arg{std::string("0700")});

    const NativePath file = scratch.path() / "file";
    const UniqueFd created{::open(file.c_str(), O_WRONLY | O_CREAT | O_CLOEXEC, 0700)};
    REQUIRE(created.valid());
    const auto not_directory = ensure_private_directory(file, own_uid());
    REQUIRE_FALSE(not_directory.has_value());
    CHECK(not_directory.error().is(kEndpointUntrusted));
    REQUIRE(not_directory.error().causes.size() == 1);
    CHECK(not_directory.error().causes.front().is(kNotADirectory));
}
