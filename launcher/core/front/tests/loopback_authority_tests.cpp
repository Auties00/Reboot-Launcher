#include <catch2/catch_test_macros.hpp>
#include <optional>
#include <string_view>

#include "reboot/front/loopback_authority.hpp"

using namespace rb;
using namespace rb::front;

namespace {

constexpr LoopbackAuthority kListener{Port{49152}};
constexpr LoopbackAuthority kXmppListener{Port{80}};

}  // namespace

TEST_CASE("Host must name the loopback listener", "[front][guard]") {
    CHECK(kListener.allows_host("127.0.0.1:49152"));
    CHECK(kListener.allows_host("LOCALHOST:49152"));
    CHECK(!kListener.allows_host("127.0.0.1:49153"));
    CHECK(!kListener.allows_host("evil.example:49152"));
    CHECK(!kListener.allows_host("127.0.0.2:49152"));
    CHECK(!kListener.allows_host(""));
}

TEST_CASE("a portless loopback Host is accepted, as UE's WebSocket client sends it", "[front][guard][ue]") {
    CHECK(kListener.allows_host("127.0.0.1"));
    CHECK(kListener.allows_host("localhost"));
    CHECK(!kListener.allows_host("evil.example"));
}

TEST_CASE("Origin is absent, the listener's own, or UE's bare loopback authority", "[front][guard][ue]") {
    CHECK(kListener.allows_origin(std::nullopt));
    CHECK(kListener.allows_origin("http://127.0.0.1:49152"));
    CHECK(kListener.allows_origin("http://localhost:49152"));
    CHECK(kListener.allows_origin("127.0.0.1"));
    CHECK(kListener.allows_origin("127.0.0.1:49152"));
    CHECK(!kListener.allows_origin("null"));
    CHECK(!kListener.allows_origin("http://127.0.0.1"));
    CHECK(!kListener.allows_origin("http://127.0.0.1:8080"));
    CHECK(!kListener.allows_origin("https://evil.example"));
    CHECK(!kListener.allows_origin("ws://127.0.0.1:49152"));
}

TEST_CASE("on port 80 the portless http origin is the listener's own", "[front][guard]") {
    CHECK(kXmppListener.allows_origin("http://127.0.0.1"));
    CHECK(kXmppListener.allows_host("127.0.0.1:80"));
}
