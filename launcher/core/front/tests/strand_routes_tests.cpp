#include <catch2/catch_test_macros.hpp>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>

#include "front_core.hpp"
#include "front_test_support.hpp"
#include "reboot/foundation/user_request.hpp"
#include "reboot/front/unencrypted_upstream_prompt.hpp"
#include "reboot/net/host_tls_memory.hpp"
#include "reboot/testing/deterministic_runtime.hpp"
#include "route_table.hpp"
#include "strand_routes.hpp"

using namespace rb;
using namespace rb::front;

namespace {

constexpr std::string_view kTicketPath = "/fortnite/api/game/v2/matchmakingservice/ticket/player/a";

class IdleConnection final : public FrontConnection {
public:
    void abort() override {}
    void drain() override {}
};

struct Routes {
    Routes() {
        REQUIRE(routes->add(FrontRoute{
            session, key, ConfiguredUpstream{*parse_upstream_origin("https://backend.example"), std::nullopt, false},
            std::nullopt}));
    }

    void learn(std::string_view service_url) {
        const std::string body = R"({"serviceUrl":")" + std::string(service_url) + R"("})";
        routes->learn(session, kTicketPath, std::span(reinterpret_cast<const u8*>(body.data()), body.size()));
    }

    [[nodiscard]] RelayVerdict verdict(std::string_view url) {
        return relay_verdict(*table.claim(key, connection), *parse_upstream_origin(url));
    }

    void accept_all() {
        for (const UserRequest& request : runtime.requests().pending())
            REQUIRE(runtime.requests().respond(request.id, UnencryptedUpstreamAnswer{true, false}));
    }

    testing::DeterministicRuntime runtime;
    RouteTable table;
    net::HostTlsMemory tls{{}, nullptr};
    std::shared_ptr<StrandRoutes> routes = std::make_shared<StrandRoutes>(table, tls, runtime.requests());
    IdleConnection connection;
    SessionId session = test::session_id(1);
    SessionKey key = test::session_key(1);
};

}  // namespace

TEST_CASE("a host seen on https loses the plain-ws consent it had", "[front][routes][tls]") {
    Routes r;
    r.learn("ws://xmpp.example:5222");
    r.learn("ws://other.example:5222");
    r.accept_all();
    REQUIRE(r.verdict("ws://xmpp.example:5222") == RelayVerdict::Allowed);

    r.routes->remember_https("XMPP.example");
    CHECK(r.verdict("ws://xmpp.example:5222") == RelayVerdict::Forbidden);
    CHECK(r.verdict("ws://other.example:5222") == RelayVerdict::Allowed);
    CHECK(r.verdict("https://backend.example") == RelayVerdict::Allowed);
}

TEST_CASE("a yes to a host seen on https while it was asked is a no", "[front][routes][tls]") {
    Routes r;
    r.learn("ws://xmpp.example:5222");
    REQUIRE(r.verdict("ws://xmpp.example:5222") == RelayVerdict::AwaitingConsent);
    r.routes->remember_https("xmpp.example");
    r.accept_all();
    CHECK(r.verdict("ws://xmpp.example:5222") == RelayVerdict::Forbidden);
}
