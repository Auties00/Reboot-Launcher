#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <string_view>

#include "messages.hpp"
#include "reboot/host/operator_policy.hpp"

using namespace rb;
using namespace rb::host;

namespace {

IpCidr cidr(std::string_view text) {
    auto parsed = IpCidr::parse(text);
    REQUIRE(parsed);
    return *parsed;
}

IpAddress address(std::string_view text) {
    auto parsed = IpAddress::parse(text);
    REQUIRE(parsed);
    return *parsed;
}

}  // namespace

TEST_CASE("a bare address is a single-address block", "[host]") {
    const IpCidr v4 = cidr("203.0.113.7");
    CHECK(v4.prefix == 32);
    CHECK(v4.to_string() == "203.0.113.7");
    CHECK(v4.contains(address("203.0.113.7")));
    CHECK_FALSE(v4.contains(address("203.0.113.8")));

    const IpCidr v6 = cidr("2001:db8::1");
    CHECK(v6.prefix == 128);
    CHECK(v6.contains(address("2001:db8::1")));
}

TEST_CASE("parsing clears host bits, so equal blocks compare equal", "[host]") {
    CHECK(cidr("203.0.113.7/24") == cidr("203.0.113.0/24"));
    CHECK(cidr("203.0.113.7/24").to_string() == "203.0.113.0/24");
    CHECK(cidr("2001:db8::ff/64") == cidr("2001:db8::/64"));
}

TEST_CASE("a block contains the addresses under its prefix", "[host]") {
    const IpCidr block = cidr("10.20.0.0/14");
    CHECK(block.contains(address("10.23.255.255")));
    CHECK_FALSE(block.contains(address("10.24.0.0")));
    CHECK(cidr("0.0.0.0/0").contains(address("198.51.100.1")));
}

TEST_CASE("an IPv4 block never matches an IPv6 address", "[host]") {
    CHECK_FALSE(cidr("0.0.0.0/0").contains(address("2001:db8::1")));
    CHECK_FALSE(cidr("::/0").contains(address("198.51.100.1")));
}

TEST_CASE("malformed blocks are refused with the text", "[host]") {
    for (const std::string_view text : {"", "host.example", "10.0.0.1/", "10.0.0.1/33", "10.0.0.1/x", "::1/129",
                                        "10.0.0.1/0024"}) {
        const auto parsed = IpCidr::parse(text);
        REQUIRE_FALSE(parsed);
        CHECK(parsed.error().is(msg::kInvalidOperatorAddress));
    }
}

TEST_CASE("an account-id ban is kept and labelled evadable", "[host]") {
    OperatorPolicy policy;
    policy.bans.push_back(HostBan{.account_id = "griefer", .reason = "spam"});
    policy.bans.push_back(HostBan{.address = cidr("198.51.100.0/24")});

    const auto normalized = normalize(policy);
    REQUIRE(normalized);
    REQUIRE(normalized->bans.size() == 2);
    CHECK(normalized->bans[0].evadable());
    CHECK_FALSE(normalized->bans[1].evadable());
}

TEST_CASE("a ban needs an address or an account id", "[host]") {
    OperatorPolicy policy;
    policy.bans.push_back(HostBan{.account_id = ""});
    const auto normalized = normalize(policy);
    REQUIRE_FALSE(normalized);
    CHECK(normalized.error().is(msg::kBanWithoutTarget));
}

TEST_CASE("normalize canonicalises blocks and drops exact repeats", "[host]") {
    OperatorPolicy policy;
    policy.operator_cidrs = {IpCidr{address("203.0.113.7"), 24}, cidr("203.0.113.0/24"), cidr("::1")};
    const auto normalized = normalize(policy);
    REQUIRE(normalized);
    CHECK(normalized->operator_cidrs == std::vector<IpCidr>{cidr("203.0.113.0/24"), cidr("::1")});
}

TEST_CASE("normalize refuses a prefix longer than the family allows", "[host]") {
    OperatorPolicy policy;
    policy.operator_cidrs = {IpCidr{address("203.0.113.7"), 33}};
    const auto normalized = normalize(policy);
    REQUIRE_FALSE(normalized);
    CHECK(normalized.error().is(msg::kInvalidOperatorAddress));
}

TEST_CASE("only bans still in force are sent", "[host]") {
    using std::chrono::hours;
    const std::chrono::system_clock::time_point now{hours{1000}};
    OperatorPolicy policy;
    policy.bans.push_back(HostBan{.address = cidr("198.51.100.1"), .expires = now - hours{1}});
    policy.bans.push_back(HostBan{.address = cidr("198.51.100.2"), .expires = now + hours{1}});
    policy.bans.push_back(HostBan{.address = cidr("198.51.100.3")});

    const auto active = active_bans(policy, now);
    REQUIRE(active.size() == 2);
    CHECK(active[0].address == cidr("198.51.100.2"));
    CHECK(active[1].address == cidr("198.51.100.3"));
}
