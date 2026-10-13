#include <optional>
#include <string>

#include <catch2/catch_test_macros.hpp>

#include "reboot/foundation/net_types.hpp"
#include "reboot/testing/expected_checks.hpp"

using namespace rb;
using rb::testing::held_error;
using rb::testing::held_value;

namespace {

std::string round_trip(std::string_view text) {
    const auto address = IpAddress::parse(text);
    return address ? address->to_string() : std::string("invalid");
}

}  // namespace

TEST_CASE("parse_port accepts 1-65535 and reports why it refused", "[foundation][net_types]") {
    CHECK(held_value(parse_port("80")) == Port{80});
    CHECK(held_value(parse_port("65535")) == Port{65535});
    CHECK(held_value(parse_port("00080")) == Port{80});
    CHECK(held_error(parse_port("")) == PortError::Empty);
    CHECK(held_error(parse_port("8a")) == PortError::NotNumeric);
    CHECK(held_error(parse_port("-1")) == PortError::NotNumeric);
    CHECK(held_error(parse_port(" 80")) == PortError::NotNumeric);
    CHECK(held_error(parse_port("0")) == PortError::OutOfRange);
    CHECK(held_error(parse_port("65536")) == PortError::OutOfRange);
    CHECK(held_error(parse_port("99999999999999999999")) == PortError::OutOfRange);
}

TEST_CASE("IPv4 parses strictly and stays IPv4-mapped", "[foundation][net_types]") {
    const auto address = IpAddress::parse("192.168.1.20");
    REQUIRE(address);
    CHECK(address->is_v4());
    CHECK(*address == IpAddress::v4(0xC0A80114));
    CHECK(address->to_string() == "192.168.1.20");
    CHECK(IpAddress::parse("127.3.2.1")->is_loopback());
    CHECK_FALSE(IpAddress::parse("128.0.0.1")->is_loopback());
    for (const char* bad : {"1.2.3", "1.2.3.4.5", "256.1.1.1", "01.2.3.4", "1..2.3", "1.2.3.4 ", "", "1234.1.1.1", "a.b.c.d"})
        CHECK_FALSE(IpAddress::parse(bad));
}

TEST_CASE("IPv6 parses every form and prints RFC 5952", "[foundation][net_types]") {
    CHECK(round_trip("::") == "::");
    CHECK(round_trip("::1") == "::1");
    CHECK(IpAddress::parse("::1")->is_loopback());
    CHECK(round_trip("2001:0DB8:0000:0000:0000:0000:0000:0001") == "2001:db8::1");
    CHECK(round_trip("2001:db8:0:0:1:0:0:1") == "2001:db8::1:0:0:1");
    CHECK(round_trip("2001:db8:0:1:1:1:1:1") == "2001:db8:0:1:1:1:1:1");
    CHECK(round_trip("fe80::") == "fe80::");
    CHECK(round_trip("1:2:3:4:5:6:7:8") == "1:2:3:4:5:6:7:8");
    CHECK(round_trip("64:ff9b::192.0.2.33") == "64:ff9b::c000:221");
    // A mapped address is the same value as its IPv4 form.
    CHECK(IpAddress::parse("::ffff:10.0.0.1") == IpAddress::parse("10.0.0.1"));
    for (const char* bad : {":", ":::", "1::2::3", "1:2:3:4:5:6:7:8:9", "1:2:3:4:5:6:7", "12345::", "1:", ":1",
                            "::g", "fe80::1%eth0", "1:2:3:4:5:6:7:1.2.3.4"})
        CHECK(round_trip(bad) == "invalid");
}

TEST_CASE("parse_host_port splits hosts, ports and bracketed IPv6", "[foundation][net_types]") {
    CHECK(held_value(parse_host_port("example.com")) == HostPort{"example.com", std::nullopt});
    CHECK(held_value(parse_host_port("example.com:7777")) == HostPort{"example.com", Port{7777}});
    CHECK(held_value(parse_host_port("10.0.0.1:80")) == HostPort{"10.0.0.1", Port{80}});
    CHECK(held_value(parse_host_port("[::1]:9000")) == HostPort{"::1", Port{9000}});
    CHECK(held_value(parse_host_port("[::1]")) == HostPort{"::1", std::nullopt});
    CHECK(held_value(parse_host_port("2001:db8::1")) == HostPort{"2001:db8::1", std::nullopt});

    CHECK(held_error(parse_host_port("")) == AddressError::Empty);
    CHECK(held_error(parse_host_port(":80")) == AddressError::BadHost);
    CHECK(held_error(parse_host_port("bad host:80")) == AddressError::BadHost);
    CHECK(held_error(parse_host_port("host:")) == AddressError::BadPort);
    CHECK(held_error(parse_host_port("host:0")) == AddressError::BadPort);
    CHECK(held_error(parse_host_port("[::1")) == AddressError::BracketMismatch);
    CHECK(held_error(parse_host_port("::1]")) == AddressError::BracketMismatch);
    CHECK(held_error(parse_host_port("[::1]x")) == AddressError::BracketMismatch);
    CHECK(held_error(parse_host_port("[1.2.3.4]:80")) == AddressError::BadHost);
    CHECK(held_error(parse_host_port("[::1]:99999")) == AddressError::BadPort);
    CHECK(held_error(parse_host_port("1:2:3")) == AddressError::BadHost);
}

TEST_CASE("Endpoint prints IPv6 in brackets", "[foundation][net_types]") {
    CHECK(Endpoint{IpAddress::v4(0x7F000001), Port{80}}.to_string() == "127.0.0.1:80");
    CHECK(Endpoint{*IpAddress::parse("::1"), Port{443}}.to_string() == "[::1]:443");
    CHECK(Endpoint{IpAddress::v4(0x7F000001), Port{80}}.is_loopback());
}
