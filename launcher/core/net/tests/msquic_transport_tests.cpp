#include <atomic>
#include <chrono>
#include <future>
#include <memory>
#include <optional>

#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/udp.hpp>
#include <catch2/catch_test_macros.hpp>

#include "reboot/net/msquic_transport.hpp"
#include "reboot/testing/fake_system_info.hpp"

using namespace rb;
using namespace rb::net;
using namespace std::chrono_literals;

namespace {

u16 closed_udp_port() {
    boost::asio::io_context io;
    boost::asio::ip::udp::socket socket(io, boost::asio::ip::udp::endpoint(boost::asio::ip::address_v4::loopback(), 0));
    return socket.local_endpoint().port();
}

ports::QuicConnectOptions options_for(u16 port) {
    ports::QuicConnectOptions options;
    options.host = "127.0.0.1";
    options.port = Port{port};
    options.alpn = "rbsb/1";
    options.ipv4_only = true;
    return options;
}

}  // namespace

TEST_CASE("MsQuic starts, and a connection nobody answers closes with a diagnostic", "[net][quic]") {
    testing::FakeSystemInfo system;
    Result<std::unique_ptr<MsQuicTransport>> transport = MsQuicTransport::create(system);
    REQUIRE(transport);

    std::promise<std::optional<Diagnostic>> closed;
    std::atomic<bool> connected{false};
    ports::QuicCallbacks callbacks;
    callbacks.on_connected = [&] { connected = true; };
    callbacks.on_closed = [&](std::optional<Diagnostic> failure) { closed.set_value(std::move(failure)); };
    auto connection = (*transport)->open_connection(options_for(closed_udp_port()), std::move(callbacks));
    REQUIRE(connection);

    std::future<std::optional<Diagnostic>> outcome = closed.get_future();
    REQUIRE(outcome.wait_for(20s) == std::future_status::ready);
    const std::optional<Diagnostic> failure = outcome.get();
    REQUIRE(failure);
    CHECK((failure->id == "net.quic_connect_failed" || failure->id == "net.quic_udp_blocked" || failure->id == "net.quic_no_ipv4"));
    CHECK_FALSE(connected);

    const Result<void> late = (*connection)->send(0, {1, 2, 3}, false);
    REQUIRE_FALSE(late);
    CHECK(late.error().id == "net.quic_stream_unknown");
    CHECK_FALSE((*connection)->open_stream());
}

TEST_CASE("connections close before the transport, or with it, without callbacks afterwards", "[net][quic][race]") {
    testing::FakeSystemInfo system;
    Result<std::unique_ptr<MsQuicTransport>> transport = MsQuicTransport::create(system);
    REQUIRE(transport);

    auto first = (*transport)->open_connection(options_for(closed_udp_port()), {});
    REQUIRE(first);
    first->reset();

    std::atomic<int> closes{0};
    ports::QuicCallbacks callbacks;
    callbacks.on_closed = [&](std::optional<Diagnostic>) { ++closes; };
    auto second = (*transport)->open_connection(options_for(closed_udp_port()), std::move(callbacks));
    REQUIRE(second);
    transport->reset();
    // The peer may have refused before the reset; nothing runs after it.
    const int seen = closes.load();
    CHECK(seen <= 1);
    CHECK_FALSE((*second)->send_datagram({1}));
    (*second)->close(0);
    second->reset();
    CHECK(closes.load() == seen);
}
