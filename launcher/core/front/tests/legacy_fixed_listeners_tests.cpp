#include <boost/asio/ip/tcp.hpp>
#include <catch2/catch_test_macros.hpp>
#include <optional>
#include <string>

#include "front_test_support.hpp"
#include "reboot/foundation/events.hpp"
#include "reboot/front/legacy_fixed_listeners.hpp"
#include "reboot/front/xmpp_unavailable.hpp"
#include "reboot/net/port_owner_service.hpp"
#include "reboot/testing/event_recorder.hpp"
#include "reboot/testing/fake_port_inspector.hpp"
#include "reboot/testing/scripted_process_launcher.hpp"

using namespace reboot;
using namespace reboot::front;
using namespace reboot::front::test;
using tcp = boost::asio::ip::tcp;

namespace {

const SessionId kSession = session_id(1);
const SessionKey kKey = session_key(0x10);

// A port nothing listens on right now.
Port free_port() {
    boost::asio::io_context io;
    tcp::acceptor acceptor(io, tcp::endpoint(boost::asio::ip::address_v4::loopback(), 0));
    return Port{acceptor.local_endpoint().port()};
}

struct LegacyFixture {
    LegacyFixture()
        : processes(h.runtime.strand(), h.runtime.clock(), testing::FakeOs::Windows), owners(inspector, processes),
          ports{free_port(), free_port()},
          listeners(h.io.io(), h.strand, h.workers, h.front, owners, h.runtime.events(), ports) {
        REQUIRE(h.front.start());
        h.front.set_embedded_backend(backend.port());
        REQUIRE(h.front.add_route(h.embedded_route(kSession, kKey)));
    }

    Result<void> open(SessionId session, bool xmpp, std::optional<Result<void>>& outcome) {
        return listeners.open(LegacyFixedRequest{session, xmpp, {}}, CancelToken{},
                              [&outcome](Result<void> result) { outcome = std::move(result); });
    }

    FrontHarness h;
    TestUpstream backend{[](const Request& request) { return respond(200, std::string(request.target())); }};
    testing::FakePortInspector inspector;
    testing::ScriptedProcessLauncher processes;
    net::PortOwnerService owners;
    LegacyFixedPorts ports;
    LegacyFixedListeners listeners;
    testing::EventRecorder recorder{h.runtime.events()};
};

}  // namespace

TEST_CASE("the fixed ports serve one session's route without a prefix", "[front][legacy]") {
    LegacyFixture f;
    std::optional<Result<void>> outcome;
    REQUIRE(f.open(kSession, true, outcome));
    REQUIRE(f.h.pump_until([&] { return outcome.has_value(); }));
    REQUIRE(*outcome);
    CHECK(f.listeners.holder() == kSession);

    TestClient client;
    auto http = client.send(f.ports.http, get("/fortnite/api/version", f.ports.http.value));
    const Reply answer = f.h.await(http);
    CHECK(answer.status == 200);
    CHECK(answer.body == "/fortnite/api/version");
    auto xmpp = client.send(f.ports.xmpp, get("/", f.ports.xmpp.value));
    CHECK(f.h.await(xmpp).status == 200);

    Request wrong_host = get("/x", f.ports.http.value);
    wrong_host.set(http::field::host, "127.0.0.1:" + std::to_string(f.h.port().value));
    auto refused = client.send(f.ports.http, std::move(wrong_host));
    CHECK(f.h.await(refused).status == 403);

    f.recorder.pump();
    CHECK(f.recorder.count(EventKind::XmppUnavailable) == 0);
}

TEST_CASE("one session at a time, and only one with a route", "[front][legacy]") {
    LegacyFixture f;
    std::optional<Result<void>> outcome;
    CHECK(f.open(session_id(9), false, outcome).error().id == "front.unknown_session");
    REQUIRE(f.open(kSession, false, outcome));
    REQUIRE(f.h.front.add_route(f.h.embedded_route(session_id(2), session_key(0x40))));
    std::optional<Result<void>> second;
    CHECK(f.open(session_id(2), false, second).error().id == "front.legacy_fixed_in_use");
    REQUIRE(f.h.pump_until([&] { return outcome.has_value(); }));
    CHECK(*outcome);

    f.listeners.close(session_id(2));
    CHECK(f.listeners.holder() == kSession);
    f.listeners.close(kSession);
    f.listeners.close(kSession);
    CHECK(!f.listeners.holder());
    REQUIRE(f.open(session_id(2), false, second));
    REQUIRE(f.h.pump_until([&] { return second.has_value(); }));
    CHECK(*second);
}

TEST_CASE("an open cancelled before it binds reports so and frees the slot", "[front][legacy]") {
    LegacyFixture f;
    CancelSource source;
    std::optional<Result<void>> outcome;
    REQUIRE(f.listeners.open(LegacyFixedRequest{kSession, false, {}}, source.token(),
                             [&outcome](Result<void> result) { outcome = std::move(result); }));
    source.cancel(CancelReason::User);
    REQUIRE(f.h.pump_until([&] { return outcome.has_value(); }));
    REQUIRE(!*outcome);
    CHECK(outcome->error().id == "front.legacy_fixed_cancelled");
    CHECK(!f.listeners.holder());

    TestClient client;
    auto http = client.send(f.ports.http, get("/x", f.ports.http.value));
    CHECK(f.h.await(http).status == 0);

    std::optional<Result<void>> retry;
    REQUIRE(f.open(kSession, false, retry));
    REQUIRE(f.h.pump_until([&] { return retry.has_value(); }));
    CHECK(*retry);
}

TEST_CASE("without the XMPP port the session hears XmppUnavailable", "[front][legacy]") {
    LegacyFixture f;
    std::optional<Result<void>> outcome;
    REQUIRE(f.open(kSession, false, outcome));
    REQUIRE(f.h.pump_until([&] { return outcome.has_value(); }));
    f.recorder.pump();
    const auto payloads = f.recorder.payloads<XmppUnavailable>(EventKind::XmppUnavailable);
    REQUIRE(payloads.size() == 1);
    CHECK(payloads[0]->session == kSession);
    CHECK(payloads[0]->reason == XmppUnavailableReason::ThirdPartyAuthDll);
    CHECK(f.recorder.events().back().session == kSession);

    TestClient client;
    auto xmpp = client.send(f.ports.xmpp, get("/", f.ports.xmpp.value));
    CHECK(f.h.await(xmpp).status == 0);
}

TEST_CASE("a held port is reported with its owner and frees the slot", "[front][legacy]") {
    LegacyFixture f;
    boost::asio::io_context io;
    tcp::acceptor squatter(io, tcp::endpoint(boost::asio::ip::address_v4::loopback(), f.ports.xmpp.value), false);
    f.inspector.set_tcp_owner(Endpoint{IpAddress::v4(0x7F000001), f.ports.xmpp}, ports::PortOwner{4242, std::nullopt, false});

    std::optional<Result<void>> outcome;
    REQUIRE(f.open(kSession, true, outcome));
    REQUIRE(f.h.pump_until([&] { return outcome.has_value(); }));
    REQUIRE(!*outcome);
    CHECK(outcome->error().id == "net.port_busy");
    CHECK(!f.listeners.holder());

    // The port that did bind was released with the failure.
    TestClient client;
    auto http = client.send(f.ports.http, get("/x", f.ports.http.value));
    CHECK(f.h.await(http).status == 0);

    std::optional<Result<void>> retry;
    REQUIRE(f.open(kSession, false, retry));
    REQUIRE(f.h.pump_until([&] { return retry.has_value(); }));
    CHECK(*retry);
}

TEST_CASE("closing the session's listeners frees the ports and ends their connections", "[front][legacy]") {
    LegacyFixture f;
    std::optional<Result<void>> outcome;
    REQUIRE(f.open(kSession, false, outcome));
    REQUIRE(f.h.pump_until([&] { return outcome.has_value(); }));
    f.listeners.close(kSession);

    boost::asio::io_context io;
    REQUIRE(f.h.pump_until([&] {
        boost::system::error_code error;
        tcp::acceptor probe(io);
        probe.open(tcp::v4(), error);
        if (!error) probe.bind(tcp::endpoint(boost::asio::ip::address_v4::loopback(), f.ports.http.value), error);
        return !error;
    }));
}
