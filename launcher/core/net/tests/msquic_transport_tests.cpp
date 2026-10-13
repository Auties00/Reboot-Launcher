#include <atomic>
#include <chrono>
#include <condition_variable>
#include <future>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>

#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/udp.hpp>
#include <catch2/catch_test_macros.hpp>

#include "quic_test_server.hpp"
#include "reboot/net/msquic_transport.hpp"
#include "reboot/testing/fake_system_info.hpp"
#include "test_pki.hpp"

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

// Dials the test server as `host`, so the certificate's DNS:localhost matches only "localhost".
ports::QuicConnectOptions server_options(u16 port, std::optional<NativePath> ca, std::string host = "localhost") {
    ports::QuicConnectOptions options = options_for(port);
    options.host = std::move(host);
    options.ca_bundle = std::move(ca);
    options.remote = IpAddress::parse("127.0.0.1");
    return options;
}

// What a connection's callbacks saw, from MsQuic's threads.
struct Observed {
    std::mutex mutex;
    std::condition_variable changed;
    bool connected = false;
    std::optional<std::optional<Diagnostic>> closed;
    std::map<u64, std::string> data;
    std::map<u64, bool> ended;

    ports::QuicCallbacks callbacks() {
        ports::QuicCallbacks out;
        out.on_connected = [this] { update([this] { connected = true; }); };
        out.on_stream_data = [this](u64 stream, std::span<const u8> bytes, bool fin) {
            update([&] {
                data[stream].append(bytes.begin(), bytes.end());
                if (fin) ended[stream] = true;
            });
        };
        out.on_closed = [this](std::optional<Diagnostic> failure) { update([&] { closed = std::move(failure); }); };
        return out;
    }

    template <class Change>
    void update(Change&& change) {
        {
            const std::scoped_lock lock(mutex);
            change();
        }
        changed.notify_all();
    }

    template <class Done>
    bool wait(Done&& done) {
        std::unique_lock lock(mutex);
        return changed.wait_for(lock, 15s, [&] { return done(); });
    }
};

std::unique_ptr<MsQuicTransport> make_transport() {
    testing::FakeSystemInfo system;
    Result<std::unique_ptr<MsQuicTransport>> transport = MsQuicTransport::create(system);
    REQUIRE(transport);
    return std::move(*transport);
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

TEST_CASE("an explicit CA bundle is the only trust anchor for the server's certificate", "[net][quic][tls]") {
    const test::TestPki pki = test::make_test_pki();
    test::QuicTestServer server(pki);
    const auto transport = make_transport();
    Observed seen;
    const auto dial = [&](const ports::QuicConnectOptions& options) {
        auto connection = transport->open_connection(options, seen.callbacks());
        REQUIRE(connection);
        return std::move(*connection);
    };
    const auto refused = [&](std::unique_ptr<ports::IQuicConnection> connection) {
        REQUIRE(seen.wait([&] { return seen.closed.has_value(); }));
        CHECK_FALSE(seen.connected);
        REQUIRE(*seen.closed);
        CHECK((*seen.closed)->id == "net.quic_connect_failed");
        connection.reset();
    };

    SECTION("signed by the bundle for the host") {
        auto connection = dial(server_options(server.port(), pki.ca_file));
        REQUIRE(seen.wait([&] { return seen.connected || seen.closed.has_value(); }));
        CHECK(seen.connected);
        connection->close(0);
        REQUIRE(seen.wait([&] { return seen.closed.has_value(); }));
        CHECK_FALSE(*seen.closed);
    }
    SECTION("no bundle: the system store does not know the test CA") {
        refused(dial(server_options(server.port(), std::nullopt)));
    }
    SECTION("a bundle that did not sign it") {
        refused(dial(server_options(server.port(), pki.other_ca_file)));
    }
    SECTION("a certificate for another host") {
        refused(dial(server_options(server.port(), pki.ca_file, "127.0.0.1")));
    }
    SECTION("a bundle that holds no certificate fails before dialling") {
        const auto missing = transport->open_connection(server_options(server.port(), pki.dir.path() / "absent.pem"), {});
        REQUIRE_FALSE(missing);
        CHECK(missing.error().id == "net.quic_connect_failed");
    }
}

TEST_CASE("a stream both sides ended is freed, and a later send on it is refused", "[net][quic]") {
    const test::TestPki pki = test::make_test_pki();
    test::QuicTestServer server(pki);
    const auto transport = make_transport();
    Observed seen;
    auto connection = transport->open_connection(server_options(server.port(), pki.ca_file), seen.callbacks());
    REQUIRE(connection);
    REQUIRE(seen.wait([&] { return seen.connected; }));

    const Result<u64> stream = (*connection)->open_stream();
    REQUIRE(stream);
    REQUIRE((*connection)->send(*stream, {'p', 'i', 'n', 'g'}, true));
    REQUIRE(seen.wait([&] { return seen.ended[*stream]; }));
    CHECK(seen.data[*stream] == "ping");

    // SHUTDOWN_COMPLETE follows the last ack on MsQuic's thread; until then the ended stream still
    // exists and refuses data for another reason.
    std::optional<Diagnostic> refusal;
    for (int i = 0; i < 2000; ++i) {
        const Result<void> late = (*connection)->send(*stream, {'x'}, false);
        REQUIRE_FALSE(late);
        refusal = late.error();
        if (refusal->id == "net.quic_stream_unknown") break;
        std::this_thread::sleep_for(5ms);
    }
    CHECK(refusal->id == "net.quic_stream_unknown");

    // The connection still works.
    const Result<u64> next = (*connection)->open_stream();
    REQUIRE(next);
    CHECK(*next == *stream + 4);
    REQUIRE((*connection)->send(*next, {'o', 'k'}, true));
    REQUIRE(seen.wait([&] { return seen.ended[*next]; }));
    CHECK(seen.data[*next] == "ok");
}

TEST_CASE("a stream the server opens delivers its data and end", "[net][quic]") {
    const test::TestPki pki = test::make_test_pki();
    test::QuicTestServer server(pki, "snapshot");
    const auto transport = make_transport();
    Observed seen;
    auto connection = transport->open_connection(server_options(server.port(), pki.ca_file), seen.callbacks());
    REQUIRE(connection);
    // The server's first unidirectional stream has id 3.
    REQUIRE(seen.wait([&] { return seen.ended[3]; }));
    CHECK(seen.data[3] == "snapshot");
}

TEST_CASE("closing a connection or its transport with streams open closes each stream once", "[net][quic][race]") {
    const test::TestPki pki = test::make_test_pki();
    test::QuicTestServer server(pki, "snapshot");
    auto transport = make_transport();
    Observed first;
    Observed second;
    auto one = transport->open_connection(server_options(server.port(), pki.ca_file), first.callbacks());
    auto two = transport->open_connection(server_options(server.port(), pki.ca_file), second.callbacks());
    REQUIRE(one);
    REQUIRE(two);
    REQUIRE(first.wait([&] { return first.connected; }));
    REQUIRE(second.wait([&] { return second.connected; }));
    for (ports::IQuicConnection* connection : {one->get(), two->get()}) {
        for (int i = 0; i < 3; ++i) {
            const Result<u64> stream = connection->open_stream();
            REQUIRE(stream);
            REQUIRE(connection->send(*stream, {'h', 'a', 'l', 'f'}, false));
        }
    }
    // A stream handle left open would keep its connection alive and hang RegistrationClose here.
    one->reset();
    transport.reset();
    CHECK_FALSE((*two)->open_stream());
    two->reset();
}
