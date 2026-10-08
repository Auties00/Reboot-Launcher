#include <catch2/catch_test_macros.hpp>

#include "reboot/front/upstream_origin.hpp"

using namespace reboot;
using namespace reboot::front;

TEST_CASE("an upstream URL reduces to its origin", "[front][origin]") {
    const auto origin = parse_upstream_origin("HTTPS://Backend.Example.com./unknown?x");
    REQUIRE(origin.has_value());
    CHECK(origin->scheme == net::UrlScheme::Https);
    CHECK(origin->host == "backend.example.com");
    CHECK(origin->port == Port{443});
    CHECK(origin->to_string() == "https://backend.example.com:443");
}

TEST_CASE("ws and wss parse as http and https", "[front][origin]") {
    CHECK(parse_upstream_origin("ws://h:81")->scheme == net::UrlScheme::Http);
    CHECK(parse_upstream_origin("wss://h")->port == Port{443});
}

TEST_CASE("an IPv6 upstream keeps its brackets only in text", "[front][origin]") {
    const auto origin = parse_upstream_origin("http://[::1]:3551");
    REQUIRE(origin.has_value());
    CHECK(origin->host == "::1");
    CHECK(origin->to_string() == "http://[::1]:3551");
}

TEST_CASE("invalid upstreams fail with front.upstream_invalid", "[front][origin]") {
    for (const char* url : {"ftp://h", "h:3551", "http://", "http://user:pw@h", "http://h:0", "http://h:99999",
                            "http://::1/", "http://127.1/", "http://0x7f000001/", "http://bad host/"}) {
        const auto origin = parse_upstream_origin(url);
        REQUIRE(!origin.has_value());
        CHECK(origin.error().id == "front.upstream_invalid");
    }
}
