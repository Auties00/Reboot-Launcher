#include <catch2/catch_test_macros.hpp>
#include <string>

#include "proxy_headers.hpp"
#include "reboot/front/front_path.hpp"

using namespace rb;
using namespace rb::front;

namespace {

UpstreamOrigin origin(std::string_view url) { return *parse_upstream_origin(url); }

const FrontBase kBase{"http://127.0.0.1:50000", "/s/00112233445566778899aabbccddeeff"};

}  // namespace

TEST_CASE("Host names the upstream as a client would", "[front][headers]") {
    CHECK(host_header(origin("https://api.example")) == "api.example");
    CHECK(host_header(origin("http://api.example:8080")) == "api.example:8080");
    CHECK(host_header(origin("https://api.example:80")) == "api.example:80");
    CHECK(host_header(origin("http://[::1]:3551")) == "[::1]:3551");
}

TEST_CASE("the relay segment reads back as the same origin", "[front][headers]") {
    for (const char* url : {"ws://127.0.0.1:80", "wss://xmpp.example", "ws://[2001:db8::1]:5222"}) {
        const UpstreamOrigin target = origin(url);
        const auto path = parse_front_path(kBase.prefix + relay_segment(target) + "/stream");
        REQUIRE(path);
        CHECK(path->relay == target);
        CHECK(path->rest == "/stream");
    }
}

TEST_CASE("only X-Reboot-* headers are ours", "[front][headers]") {
    CHECK(is_reboot_header("X-Reboot-Session"));
    CHECK(is_reboot_header("x-reboot-xmpp"));
    CHECK(!is_reboot_header("X-Rebooted"));
    CHECK(!is_reboot_header("X-Epic-Device-Id"));
}

TEST_CASE("Location is mapped only when it names the upstream or the same host", "[front][headers]") {
    const UpstreamOrigin upstream = origin("https://api.example");
    CHECK(rewrite_location("https://api.example/account?x=1", upstream, kBase) ==
          "http://127.0.0.1:50000/s/00112233445566778899aabbccddeeff/account?x=1");
    CHECK(rewrite_location("https://API.example:443", upstream, kBase) ==
          "http://127.0.0.1:50000/s/00112233445566778899aabbccddeeff/");
    CHECK(rewrite_location("/next", upstream, kBase) == "/s/00112233445566778899aabbccddeeff/next");
    CHECK(!rewrite_location("http://api.example/downgrade", upstream, kBase));
    CHECK(!rewrite_location("https://other.example/x", upstream, kBase));
    CHECK(!rewrite_location("//api.example/x", upstream, kBase));
    CHECK(!rewrite_location("next", upstream, kBase));
    CHECK(!rewrite_location("/next", upstream, FrontBase{"http://127.0.0.1:3551", ""}));
}
