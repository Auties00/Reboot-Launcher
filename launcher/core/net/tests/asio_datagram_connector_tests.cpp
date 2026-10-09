#include <array>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <optional>
#include <thread>
#include <vector>

#include <boost/asio/executor_work_guard.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/udp.hpp>
#include <catch2/catch_test_macros.hpp>

#include "reboot/net/datagram_connector.hpp"

using namespace reboot;
using namespace reboot::net;
namespace asio = boost::asio;

namespace {

// Real loopback sockets on an I/O thread, as the engine runs them.
struct IoThread {
    IoThread() : guard(asio::make_work_guard(io)), thread([this] { io.run(); }) {}
    ~IoThread() {
        guard.reset();
        io.stop();
        thread.join();
    }

    asio::io_context io;
    asio::executor_work_guard<asio::io_context::executor_type> guard;
    std::thread thread;
};

struct Seen {
    std::mutex mutex;
    std::condition_variable changed;
    std::vector<std::vector<u8>> datagrams;
    std::optional<DatagramFailure> failure;

    DatagramCallbacks callbacks() {
        DatagramCallbacks out;
        out.on_datagram = [this](std::span<const u8> bytes) {
            const std::scoped_lock lock(mutex);
            datagrams.emplace_back(bytes.begin(), bytes.end());
            changed.notify_all();
        };
        out.on_failure = [this](DatagramFailure kind, std::optional<SystemError>) {
            const std::scoped_lock lock(mutex);
            failure = kind;
            changed.notify_all();
        };
        return out;
    }

    template <class Condition>
    bool wait(Condition condition) {
        std::unique_lock lock(mutex);
        return changed.wait_for(lock, std::chrono::seconds{5}, condition);
    }
};

Endpoint loopback(u16 port) { return Endpoint{IpAddress::v4(0x7F000001), Port{port}}; }

}  // namespace

TEST_CASE("a connected channel sends and receives only its peer's datagrams", "[net][udp]") {
    IoThread io;
    asio::io_context peer_io;
    asio::ip::udp::socket peer(peer_io, asio::ip::udp::endpoint(asio::ip::address_v4::loopback(), 0));
    asio::ip::udp::socket stranger(peer_io, asio::ip::udp::endpoint(asio::ip::address_v4::loopback(), 0));
    const std::unique_ptr<IDatagramConnector> connector = make_asio_datagram_connector(io.io);
    Seen seen;
    auto channel = connector->connect(loopback(peer.local_endpoint().port()), seen.callbacks());
    REQUIRE(channel);

    const std::array<u8, 3> ping{1, 2, 3};
    (*channel)->send(ping);
    std::array<u8, 16> buffer{};
    asio::ip::udp::endpoint from;
    const std::size_t size = peer.receive_from(asio::buffer(buffer), from);
    CHECK(size == 3);

    const std::array<u8, 1> wrong{9};
    stranger.send_to(asio::buffer(wrong), from);
    const std::array<u8, 2> pong{4, 5};
    peer.send_to(asio::buffer(pong), from);
    REQUIRE(seen.wait([&] { return !seen.datagrams.empty(); }));
    const std::scoped_lock lock(seen.mutex);
    REQUIRE(seen.datagrams.size() == 1);
    CHECK(seen.datagrams.front() == std::vector<u8>{4, 5});
}

TEST_CASE("an ICMP port unreachable is reported as Refused, and nothing runs after close", "[net][udp]") {
    IoThread io;
    u16 closed_port = 0;
    {
        asio::io_context scratch;
        asio::ip::udp::socket probe(scratch, asio::ip::udp::endpoint(asio::ip::address_v4::loopback(), 0));
        closed_port = probe.local_endpoint().port();
    }
    const std::unique_ptr<IDatagramConnector> connector = make_asio_datagram_connector(io.io);
    Seen seen;
    auto channel = connector->connect(loopback(closed_port), seen.callbacks());
    REQUIRE(channel);
    const std::array<u8, 1> ping{1};
    (*channel)->send(ping);
    REQUIRE(seen.wait([&] { return seen.failure.has_value(); }));
    CHECK(*seen.failure == DatagramFailure::Refused);

    channel->reset();
    const std::scoped_lock lock(seen.mutex);
    seen.failure.reset();
}
