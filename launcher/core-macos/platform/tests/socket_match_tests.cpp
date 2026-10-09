#include <catch2/catch_test_macros.hpp>

#include <vector>

#include "socket_match.hpp"

using namespace rb::os_macos::platform;
using rb::Endpoint;
using rb::IpAddress;
using rb::NativePath;
using rb::Port;

namespace {

const IpAddress kLoopback4 = IpAddress::v4(0x7F000001);
const IpAddress kAny4 = IpAddress::v4(0);

}  // namespace

TEST_CASE("a v4 listener owns its own address and port only", "[socket_match]") {
    const LocalSocket socket{.tcp = true, .v4 = true, .v6 = false, .address = kLoopback4, .port = Port{3551}};
    CHECK(owns_tcp(socket, Endpoint{kLoopback4, Port{3551}}));
    CHECK_FALSE(owns_tcp(socket, Endpoint{IpAddress::v4(0x0A000001), Port{3551}}));
    CHECK_FALSE(owns_tcp(socket, Endpoint{kLoopback4, Port{3552}}));
    CHECK_FALSE(owns_udp(socket, Port{3551}));
}

TEST_CASE("a v4 wildcard owns every v4 address of its port", "[socket_match]") {
    const LocalSocket socket{.tcp = true, .v4 = true, .v6 = false, .address = kAny4, .port = Port{7777}};
    CHECK(owns_tcp(socket, Endpoint{kLoopback4, Port{7777}}));
    CHECK_FALSE(owns_tcp(socket, Endpoint{*IpAddress::parse("::1"), Port{7777}}));
}

TEST_CASE("a dual-stack v6 wildcard also owns a v4 endpoint", "[socket_match]") {
    const LocalSocket dual{.tcp = true, .v4 = true, .v6 = true, .address = IpAddress{}, .port = Port{7777}};
    CHECK(owns_tcp(dual, Endpoint{kLoopback4, Port{7777}}));
    CHECK(owns_tcp(dual, Endpoint{*IpAddress::parse("::1"), Port{7777}}));
    const LocalSocket v6_only{.tcp = true, .v4 = false, .v6 = true, .address = IpAddress{}, .port = Port{7777}};
    CHECK_FALSE(owns_tcp(v6_only, Endpoint{kLoopback4, Port{7777}}));
    CHECK(owns_tcp(v6_only, Endpoint{*IpAddress::parse("::1"), Port{7777}}));
}

TEST_CASE("UDP ownership is by port", "[socket_match]") {
    const LocalSocket socket{.tcp = false, .v4 = true, .v6 = false, .address = kAny4, .port = Port{7777}};
    CHECK(owns_udp(socket, Port{7777}));
    CHECK_FALSE(owns_udp(socket, Port{7778}));
    CHECK_FALSE(owns_tcp(socket, Endpoint{kLoopback4, Port{7777}}));
}

TEST_CASE("another holder beats wineserver, which is still marked", "[socket_match]") {
    const std::vector<Candidate> wine{{.pid = 10, .exe = NativePath{"/rt/bin/wineserver"}},
                                      {.pid = 11, .exe = NativePath{"/rt/bin/wine-preloader"}}};
    const auto owner = choose_owner(wine);
    REQUIRE(owner);
    CHECK(owner->pid == 11);
    CHECK(owner->wine_server);

    const std::vector<Candidate> server_only{{.pid = 10, .exe = NativePath{"/rt/bin/wineserver"}}};
    const auto server = choose_owner(server_only);
    REQUIRE(server);
    CHECK(server->pid == 10);
    CHECK(server->wine_server);

    const std::vector<Candidate> native{{.pid = 5, .exe = std::nullopt}};
    const auto plain = choose_owner(native);
    REQUIRE(plain);
    CHECK(plain->pid == 5);
    CHECK_FALSE(plain->wine_server);
    CHECK_FALSE(choose_owner({}));
}
