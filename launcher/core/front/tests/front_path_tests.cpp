#include <catch2/catch_test_macros.hpp>
#include <string>

#include "reboot/front/front_path.hpp"

using namespace rb;
using namespace rb::front;

namespace {

const std::string kKey = "0123456789abcdef0123456789ABCDEF";

}  // namespace

TEST_CASE("a routed path keeps everything after the key", "[front][path]") {
    const auto path = parse_front_path("/s/" + kKey + "/account/api/oauth/token?x=1");
    REQUIRE(path.has_value());
    CHECK(path->key.to_hex() == "0123456789abcdef0123456789abcdef");
    CHECK(!path->relay.has_value());
    CHECK(path->rest == "/account/api/oauth/token?x=1");
}

TEST_CASE("a bare key routes to the upstream root", "[front][path]") {
    CHECK(parse_front_path("/s/" + kKey)->rest == "/");
    CHECK(parse_front_path("/s/" + kKey + "?q")->rest == "/?q");
}

TEST_CASE("anything outside /s/<32 hex digits> is not a route", "[front][path]") {
    CHECK(!parse_front_path("/account/api/oauth/token"));
    CHECK(!parse_front_path("/s/0123/"));
    CHECK(!parse_front_path("/s/" + kKey + "0/"));
    CHECK(!parse_front_path("/s/0123456789abcdef0123456789abcdeg/"));
}

TEST_CASE("a relay path carries the rewritten WebSocket authority", "[front][path][relay]") {
    const auto path = parse_front_path("/s/" + kKey + "/@/wss/XMPP.example.com/xmpp?v=1");
    REQUIRE(path.has_value());
    REQUIRE(path->relay.has_value());
    CHECK(path->relay->scheme == net::UrlScheme::Https);
    CHECK(path->relay->host == "xmpp.example.com");
    CHECK(path->relay->port == Port{443});
    CHECK(path->rest == "/xmpp?v=1");

    const auto local = parse_front_path("/s/" + kKey + "/@/ws/127.0.0.1:80");
    REQUIRE(local.has_value());
    CHECK(local->relay->port == Port{80});
    CHECK(local->rest == "/");
}

TEST_CASE("a malformed relay segment is not a route", "[front][path][relay]") {
    CHECK(!parse_front_path("/s/" + kKey + "/@/http/host/"));
    CHECK(!parse_front_path("/s/" + kKey + "/@/ws"));
    CHECK(!parse_front_path("/s/" + kKey + "/@/ws//"));
    CHECK(!parse_front_path("/s/" + kKey + "/@/ws/host:0/"));
    CHECK(!parse_front_path("/s/" + kKey + "/@/ws/127.1/"));
}
