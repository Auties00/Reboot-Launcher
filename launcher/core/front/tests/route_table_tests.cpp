#include <catch2/catch_test_macros.hpp>
#include <memory>

#include "front_core.hpp"
#include "front_test_support.hpp"
#include "route_table.hpp"

using namespace rb;
using namespace rb::front;

namespace {

UpstreamOrigin origin(std::string_view url) { return *parse_upstream_origin(url); }

class CountingConnection final : public FrontConnection {
public:
    void abort() override { ++aborts; }
    void drain() override { ++drains; }

    int aborts = 0;
    int drains = 0;
};

RouteSnapshot configured(const UpstreamOrigin& backend) {
    RouteSnapshot route;
    route.session = test::session_id(1);
    route.key = test::session_key(1);
    route.configured = ConfiguredView{UpstreamPolicy(backend), std::nullopt, false, {}, {}};
    return route;
}

}  // namespace

TEST_CASE("relays reach the backend and learned origins, plain ones once consented", "[front][table]") {
    RouteSnapshot route = configured(origin("http://backend.example:8080"));
    UpstreamPolicy& policy = route.configured->policy;
    const std::string ticket = R"({"serviceUrl":"ws://mms.example:9000"})";
    const std::string secure = R"({"serviceUrl":"wss://secure.example"})";
    const std::string_view path = "/fortnite/api/game/v2/matchmakingservice/ticket/player/a";
    static_cast<void>(policy.learn(path, std::span(reinterpret_cast<const u8*>(ticket.data()), ticket.size())));
    static_cast<void>(policy.learn(path, std::span(reinterpret_cast<const u8*>(secure.data()), secure.size())));

    CHECK(relay_verdict(route, origin("http://backend.example:8080")) == RelayVerdict::Allowed);
    CHECK(relay_verdict(route, origin("wss://secure.example")) == RelayVerdict::Allowed);
    CHECK(relay_verdict(route, origin("ws://mms.example:9000")) == RelayVerdict::AwaitingConsent);
    CHECK(relay_verdict(route, origin("ws://never.example:9000")) == RelayVerdict::Forbidden);

    route.configured->consented.push_back(origin("ws://mms.example:9000"));
    CHECK(relay_verdict(route, origin("ws://mms.example:9000")) == RelayVerdict::Allowed);
    route.configured->consented.clear();
    route.configured->declined.push_back(origin("ws://mms.example:9000"));
    CHECK(relay_verdict(route, origin("ws://mms.example:9000")) == RelayVerdict::Forbidden);

    RouteSnapshot embedded;
    CHECK(relay_verdict(embedded, origin("ws://127.0.0.1:80")) == RelayVerdict::Forbidden);
}

TEST_CASE("the lease's plain consent covers relays to the backend's host", "[front][table]") {
    RouteSnapshot route = configured(origin("http://backend.example:8080"));
    route.configured->plain_http_consented = true;
    const std::string ticket = R"({"serviceUrl":"ws://backend.example:9000"})";
    static_cast<void>(route.configured->policy.learn("/fortnite/api/game/v2/matchmakingservice/ticket/x",
                                                     std::span(reinterpret_cast<const u8*>(ticket.data()), ticket.size())));
    CHECK(relay_verdict(route, origin("ws://backend.example:9000")) == RelayVerdict::Allowed);
}

TEST_CASE("the pin covers the backend's host only", "[front][table]") {
    RouteSnapshot route = configured(origin("https://backend.example"));
    route.configured->pin = std::array<u8, 32>{1};
    CHECK(pin_for(route, origin("wss://backend.example:5222")));
    CHECK(!pin_for(route, origin("wss://other.example")));
}

TEST_CASE("a route's removal hands back the connections that served it", "[front][table]") {
    RouteTable table;
    auto route = std::make_shared<RouteSnapshot>(configured(origin("https://backend.example")));
    table.put(route);
    auto served = std::make_shared<CountingConnection>();
    auto idle = std::make_shared<CountingConnection>();
    REQUIRE(table.enter(served, ListenerKind::Session));
    REQUIRE(table.enter(idle, ListenerKind::Session));
    CHECK(!table.enter(std::make_shared<CountingConnection>(), ListenerKind::Fixed));

    CHECK(table.claim(test::session_key(1), *served) == route);
    CHECK(!table.claim(test::session_key(2), *idle));
    const auto closed = table.erase(route->session);
    REQUIRE(closed.size() == 1);
    CHECK(closed[0] == served);
    CHECK(!table.claim(test::session_key(1), *served));
}

TEST_CASE("fixed-listener connections follow the legacy session", "[front][table]") {
    RouteTable table;
    auto route = std::make_shared<RouteSnapshot>(configured(origin("https://backend.example")));
    table.put(route);
    static_cast<void>(table.set_legacy(route->session, {Port{3551}}));
    table.set_session_port(Port{50000});
    CHECK(table.front_ports().size() == 2);
    auto fixed = std::make_shared<CountingConnection>();
    REQUIRE(table.enter(fixed, ListenerKind::Fixed));
    CHECK(table.claim_legacy(*fixed) == route);

    const auto closed = table.set_legacy(std::nullopt, {});
    REQUIRE(closed.size() == 1);
    CHECK(closed[0] == fixed);
    CHECK(!table.claim_legacy(*fixed));
    CHECK(table.front_ports() == std::vector<Port>{Port{50000}});
}

TEST_CASE("stopping refuses new connections and reports when the last one leaves", "[front][table]") {
    RouteTable table;
    auto connection = std::make_shared<CountingConnection>();
    REQUIRE(table.enter(connection, ListenerKind::Session));
    bool drained = false;
    CHECK(table.begin_stop([&] { drained = true; }).size() == 1);
    CHECK(!drained);
    CHECK(!table.enter(std::make_shared<CountingConnection>(), ListenerKind::Session));
    table.leave(*connection);
    CHECK(drained);

    table.end_stop();
    CHECK(table.enter(connection, ListenerKind::Session));
}
