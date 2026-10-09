#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cerrno>
#include <fcntl.h>
#include <memory>
#include <poll.h>
#include <signal.h>
#include <string>
#include <sys/socket.h>
#include <thread>
#include <utility>
#include <vector>

#include "posix_test_support.hpp"
#include "reboot/posix/ignore_sigpipe.hpp"
#include "reboot/posix/unique_fd.hpp"
#include "socket_fds.hpp"
#include "socket_stream.hpp"
#include "unistd.hpp"

using namespace rb;
using namespace rb::posix;
using namespace rb::posix::test;

namespace {

struct FdPair {
    UniqueFd a;
    UniqueFd b;
};

// Close-on-exec and non-blocking, as make_unix_stream_socket and accept_unix_stream hand out.
[[nodiscard]] FdPair make_socket_pair() {
    // A socketpair has no SO_NOSIGPIPE on macOS.
    REQUIRE(ignore_sigpipe().has_value());
    std::array<int, 2> fds{-1, -1};
    REQUIRE(::socketpair(AF_UNIX, SOCK_STREAM, 0, fds.data()) == 0);
    FdPair pair{UniqueFd{fds[0]}, UniqueFd{fds[1]}};
    REQUIRE(make_cloexec_nonblocking(pair.a.get()).has_value());
    REQUIRE(make_cloexec_nonblocking(pair.b.get()).has_value());
    return pair;
}

struct StreamPair {
    std::unique_ptr<SocketStream> a;
    std::unique_ptr<SocketStream> b;
};

[[nodiscard]] StreamPair make_stream_pair() {
    FdPair fds = make_socket_pair();
    auto a = SocketStream::start(std::move(fds.a), ports::PeerIdentity{.user_id = "501", .pid = 7});
    auto b = SocketStream::start(std::move(fds.b), ports::PeerIdentity{.user_id = "501", .pid = 8});
    REQUIRE(a.has_value());
    REQUIRE(b.has_value());
    return {std::move(*a), std::move(*b)};
}

[[nodiscard]] std::shared_ptr<StreamLog> watch(SocketStream& stream) {
    auto log = std::make_shared<StreamLog>();
    stream.on_read([log](std::span<const u8> bytes) { log->append(bytes); });
    stream.on_close([log] { log->closed(); });
    return log;
}

[[nodiscard]] bool readable_now(int fd) {
    pollfd entry{.fd = fd, .events = POLLIN, .revents = 0};
    return ::poll(&entry, 1, 0) == 1;
}

}  // namespace

TEST_CASE("ignore_sigpipe sets SIGPIPE to ignored") {
    REQUIRE(ignore_sigpipe().has_value());
    struct sigaction current {};
    REQUIRE(::sigaction(SIGPIPE, nullptr, &current) == 0);
    CHECK(current.sa_handler == SIG_IGN);
}

TEST_CASE("WakePipe never blocks on a full pipe and drains to empty") {
    auto wake = WakePipe::create();
    REQUIRE(wake.has_value());
    CHECK((::fcntl(wake->read_fd(), F_GETFD) & FD_CLOEXEC) != 0);
    CHECK_FALSE(readable_now(wake->read_fd()));

    // Far beyond any pipe buffer; a blocking write would hang here.
    for (int i = 0; i < 100'000; ++i) wake->wake();
    CHECK(readable_now(wake->read_fd()));
    wake->drain();
    CHECK_FALSE(readable_now(wake->read_fd()));
    wake->wake();
    CHECK(readable_now(wake->read_fd()));
}

TEST_CASE("unix stream sockets are close-on-exec and non-blocking, and report would_block and EOF") {
    const auto scratch = make_scratch("posix-fds");
    const NativePath path = scratch.path() / "s.sock";
    auto listening = make_unix_stream_socket();
    REQUIRE(listening.has_value());
    CHECK((::fcntl(listening->get(), F_GETFD) & FD_CLOEXEC) != 0);
    CHECK((::fcntl(listening->get(), F_GETFL) & O_NONBLOCK) != 0);
    REQUIRE(bind_unix(listening->get(), path) == 0);
    REQUIRE(::listen(listening->get(), 4) == 0);

    auto none = accept_unix_stream(listening->get());
    REQUIRE(none.has_value());
    CHECK_FALSE(none->valid());

    auto client = make_unix_stream_socket();
    REQUIRE(client.has_value());
    REQUIRE(connect_unix(client->get(), path) == 0);
    auto server = accept_unix_stream(listening->get());
    REQUIRE(server.has_value());
    REQUIRE(server->valid());
    CHECK((::fcntl(server->get(), F_GETFD) & FD_CLOEXEC) != 0);
    CHECK((::fcntl(server->get(), F_GETFL) & O_NONBLOCK) != 0);

    std::array<u8, 16> buffer{};
    const IoResult empty = receive_some(server->get(), buffer);
    CHECK(empty.would_block);
    CHECK(empty.error == 0);

    const IoResult sent = send_some(client->get(), as_bytes("ping"));
    CHECK(sent.bytes == 4);
    const IoResult got = receive_some(server->get(), buffer);
    CHECK(got.bytes == 4);
    CHECK(std::string(buffer.begin(), buffer.begin() + 4) == "ping");

    client->reset();
    const IoResult eof = receive_some(server->get(), buffer);
    CHECK(eof.bytes == 0);
    CHECK_FALSE(eof.would_block);
    CHECK(eof.error == 0);

    // The peer is gone: an error, not a SIGPIPE.
    const IoResult broken = send_some(server->get(), as_bytes("pong"));
    CHECK(broken.error != 0);

    auto stray = make_unix_stream_socket();
    REQUIRE(stray.has_value());
    CHECK(connect_unix(stray->get(), scratch.path() / "absent.sock") == ENOENT);
}

TEST_CASE("SocketStream carries bytes both ways in order and reports its peer") {
    auto [a, b] = make_stream_pair();
    const auto a_log = watch(*a);
    const auto b_log = watch(*b);
    CHECK(a->peer().user_id == "501");
    CHECK(a->peer().pid == 7);

    a->write(as_bytes("abc"));
    a->write(as_bytes(""));
    a->write(as_bytes("def"));
    b->write(as_bytes("xyz"));
    REQUIRE(wait_for([&] { return b_log->size() >= 6 && a_log->size() >= 3; }));
    CHECK(b_log->received() == "abcdef");
    CHECK(a_log->received() == "xyz");
}

TEST_CASE("SocketStream delivers bytes that arrived before on_read was set") {
    auto [a, b] = make_stream_pair();
    a->write(as_bytes("early"));
    const auto b_log = watch(*b);
    REQUIRE(wait_for([&] { return b_log->received() == "early"; }));
}

TEST_CASE("SocketStream delivers what a peer sent before hanging up, then closes once") {
    auto [a, b] = make_stream_pair();
    auto b_log = std::make_shared<StreamLog>();
    b->on_close([b_log] { b_log->closed(); });
    a->write(as_bytes("bye"));
    a->close();
    // The socket is polled only once there is a reader.
    b->on_read([b_log](std::span<const u8> bytes) { b_log->append(bytes); });
    REQUIRE(wait_for([&] { return b_log->closes() == 1; }));
    CHECK(b_log->received() == "bye");
}

TEST_CASE("SocketStream fires on_close once on each end, however often and from wherever it is closed") {
    auto [a, b] = make_stream_pair();
    const auto a_log = watch(*a);
    const auto b_log = watch(*b);

    std::thread closer{[&] { a->close(); }};
    b->close();
    a->close();
    closer.join();
    REQUIRE(wait_for([&] { return a_log->closes() == 1 && b_log->closes() == 1; }));
    a->write(as_bytes("ignored"));
    a->close();
    // Destruction joins the stream threads, so any late callback has run by now.
    a.reset();
    b.reset();
    CHECK(a_log->closes() == 1);
    CHECK(b_log->closes() == 1);
    CHECK(b_log->received().empty());
}

TEST_CASE("a write to a peer that hung up still lets SocketStream read what the peer sent first") {
    FdPair fds = make_socket_pair();
    auto stream = SocketStream::start(std::move(fds.b), ports::PeerIdentity{.user_id = "501", .pid = 8});
    REQUIRE(stream.has_value());
    REQUIRE(send_some(fds.a.get(), as_bytes("goodbye")).bytes == 7);
    fds.a.reset();

    // EPIPE: the peer is gone, but its last bytes still wait in the socket.
    (*stream)->write(as_bytes("ping"));
    (*stream)->write(as_bytes("pong"));
    const auto log = watch(**stream);
    REQUIRE(wait_for([&] { return log->closes() == 1; }));
    CHECK(log->received() == "goodbye");
}

TEST_CASE("SocketStream fires on_close set after the stream already finished") {
    auto [a, b] = make_stream_pair();
    a->close();
    auto a_log = std::make_shared<StreamLog>();
    a->on_close([a_log] { a_log->closed(); });
    REQUIRE(wait_for([&] { return a_log->closes() == 1; }));
}

TEST_CASE("SocketStream queues a write larger than the socket buffer and delivers it intact") {
    auto [a, b] = make_stream_pair();
    std::string big(8 * 1024 * 1024 + 3, '\0');
    for (std::size_t i = 0; i < big.size(); ++i) big[i] = static_cast<char>(i * 31 + 7);
    const auto b_log = watch(*b);

    a->write(as_bytes(big));
    a->write(as_bytes("tail"));
    REQUIRE(wait_for([&] { return b_log->size() >= big.size() + 4; }));
    const std::string received = b_log->received();
    CHECK(received.size() == big.size() + 4);
    CHECK(received.compare(0, big.size(), big) == 0);
    CHECK(received.compare(big.size(), 4, "tail") == 0);
}

TEST_CASE("SocketStream keeps each write whole when several threads write at once") {
    auto [a, b] = make_stream_pair();
    const auto b_log = watch(*b);
    constexpr std::size_t kThreads = 4;
    constexpr std::size_t kWrites = 300;
    constexpr std::size_t kBlock = 1024;

    std::vector<std::thread> writers;
    for (std::size_t t = 0; t < kThreads; ++t) {
        writers.emplace_back([&, t] {
            const std::string block(kBlock, static_cast<char>('a' + t));
            for (std::size_t i = 0; i < kWrites; ++i) a->write(as_bytes(block));
        });
    }
    for (std::thread& writer : writers) writer.join();
    REQUIRE(wait_for([&] { return b_log->size() >= kThreads * kWrites * kBlock; }));

    const std::string received = b_log->received();
    REQUIRE(received.size() == kThreads * kWrites * kBlock);
    std::array<std::size_t, kThreads> blocks{};
    bool whole = true;
    for (std::size_t offset = 0; offset < received.size(); offset += kBlock) {
        const char tag = received[offset];
        whole = whole && received.find_first_not_of(tag, offset) >= offset + kBlock;
        if (tag >= 'a' && tag < static_cast<char>('a' + kThreads)) ++blocks[static_cast<std::size_t>(tag - 'a')];
    }
    CHECK(whole);
    for (const std::size_t count : blocks) CHECK(count == kWrites);
}

TEST_CASE("SocketStream lets a read callback write and replace itself") {
    auto [a, b] = make_stream_pair();
    auto a_log = watch(*a);
    SocketStream* echo = b.get();
    auto replaced = std::make_shared<StreamLog>();
    b->on_read([echo, replaced](std::span<const u8> bytes) {
        echo->write(bytes);
        echo->on_read([replaced](std::span<const u8> later) { replaced->append(later); });
    });

    a->write(as_bytes("one"));
    REQUIRE(wait_for([&] { return a_log->received() == "one"; }));
    a->write(as_bytes("two"));
    REQUIRE(wait_for([&] { return replaced->received() == "two"; }));
    CHECK(a_log->received() == "one");
}

TEST_CASE("destroying a SocketStream without close ends the peer's stream") {
    auto [a, b] = make_stream_pair();
    const auto b_log = watch(*b);
    a.reset();
    REQUIRE(wait_for([&] { return b_log->closes() == 1; }));
}
