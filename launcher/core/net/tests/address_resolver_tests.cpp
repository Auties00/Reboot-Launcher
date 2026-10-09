#include <chrono>
#include <optional>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "reboot/foundation/cancel.hpp"
#include "reboot/net/address_resolver.hpp"
#include "reboot/testing/deterministic_runtime.hpp"
#include "reboot/testing/fake_resolver.hpp"

using namespace rb;
using namespace rb::net;
using namespace std::chrono_literals;

namespace {

struct Fixture {
    testing::DeterministicRuntime runtime;
    testing::FakeResolver resolver{runtime.strand()};
    AddressResolver addresses{resolver, runtime.strand(), runtime.timers()};
    std::optional<Result<ResolvedAddress>> result;
    int calls = 0;

    Result<void> resolve(std::string_view text, AddressFamilyPolicy family = AddressFamilyPolicy::Any, CancelToken token = {}) {
        return addresses.resolve(text, kDefaultGamePort, family, std::move(token), [this](Result<ResolvedAddress> answer) {
            ++calls;
            result = std::move(answer);
        });
    }
};

IpAddress ip(std::string_view text) { return *IpAddress::parse(text); }

}  // namespace

TEST_CASE("local aliases and IP literals resolve without the resolver", "[net][resolve]") {
    for (const std::string_view alias : {"localhost", "0.0.0.0", "127.0.0.1:7778"}) {
        Fixture f;
        REQUIRE(f.resolve(alias));
        CHECK(f.calls == 0);
        f.runtime.run_until_idle();
        REQUIRE(f.calls == 1);
        REQUIRE(f.result->has_value());
        REQUIRE((*f.result)->endpoints.size() == 1);
        CHECK((*f.result)->endpoints.front().address == ip("127.0.0.1"));
        CHECK(f.resolver.queries().empty());
    }

    Fixture literal;
    REQUIRE(literal.resolve("[2001:db8::1]:9000"));
    literal.runtime.run_until_idle();
    REQUIRE(literal.result->has_value());
    CHECK((*literal.result)->endpoints.front() == Endpoint{ip("2001:db8::1"), Port{9000}});
    CHECK((*literal.result)->parsed.port == Port{9000});
}

TEST_CASE("names go through the resolver in its order, without duplicates", "[net][resolve]") {
    Fixture f;
    f.resolver.set("play.test", {ip("10.0.0.2"), ip("2001:db8::2"), ip("10.0.0.2"), ip("10.0.0.3")});
    REQUIRE(f.resolve("play.test"));
    f.runtime.run_until_idle();
    REQUIRE(f.result->has_value());
    const std::vector<Endpoint>& endpoints = (*f.result)->endpoints;
    REQUIRE(endpoints.size() == 3);
    CHECK(endpoints[0] == Endpoint{ip("10.0.0.2"), kDefaultGamePort});
    CHECK(endpoints[1].address == ip("2001:db8::2"));
    CHECK(endpoints[2].address == ip("10.0.0.3"));

    Fixture v4;
    v4.resolver.set("play.test", {ip("2001:db8::2"), ip("10.0.0.2")});
    REQUIRE(v4.resolve("play.test:7000", AddressFamilyPolicy::Ipv4Only));
    v4.runtime.run_until_idle();
    REQUIRE(v4.result->has_value());
    REQUIRE((*v4.result)->endpoints.size() == 1);
    CHECK((*v4.result)->endpoints.front() == Endpoint{ip("10.0.0.2"), Port{7000}});
}

TEST_CASE("resolution failures map to ResolveError diagnostics", "[net][resolve]") {
    Fixture invalid;
    const Result<void> bad = invalid.resolve("[::1");
    REQUIRE_FALSE(bad);
    CHECK(bad.error().id == "net.address_invalid");

    Fixture v6_only;
    v6_only.resolver.set("v6.test", {ip("2001:db8::9")});
    REQUIRE(v6_only.resolve("v6.test", AddressFamilyPolicy::Ipv4Only));
    v6_only.runtime.run_until_idle();
    CHECK(v6_only.result->error().id == "net.no_ipv4_address");

    Fixture literal_v6;
    REQUIRE(literal_v6.resolve("[::2]:1", AddressFamilyPolicy::Ipv4Only));
    literal_v6.runtime.run_until_idle();
    CHECK(literal_v6.result->error().id == "net.no_ipv4_address");

    Fixture missing;
    REQUIRE(missing.resolve("nothing.invalid"));
    missing.runtime.run_until_idle();
    CHECK(missing.result->error().id == "net.host_not_found");

    Fixture failed;
    failed.resolver.fail("broken.test", make_diag(ErrorDomain::Platform, MessageId{"platform.io"}).build());
    REQUIRE(failed.resolve("broken.test"));
    failed.runtime.run_until_idle();
    CHECK(failed.result->error().id == "net.resolve_failed");
    REQUIRE(failed.result->error().causes.size() == 1);
    CHECK(failed.result->error().causes.front().id == "platform.io");
}

TEST_CASE("a hanging lookup ends at the Dns deadline and cancels the resolver", "[net][resolve][race]") {
    Fixture f;
    f.resolver.hang("slow.test");
    REQUIRE(f.resolve("slow.test"));
    f.runtime.advance(4999ms);
    CHECK(f.calls == 0);
    f.runtime.advance(1ms);
    REQUIRE(f.calls == 1);
    CHECK(f.result->error().id == "net.resolve_timeout");
    f.runtime.advance(1s);
    CHECK(f.calls == 1);
    CHECK(f.resolver.pending() == 0);
}

TEST_CASE("cancelling a lookup completes it once with net.resolve_cancelled", "[net][resolve][race]") {
    Fixture f;
    f.resolver.hang("slow.test");
    CancelSource source;
    REQUIRE(f.resolve("slow.test", AddressFamilyPolicy::Any, source.token()));
    f.runtime.run_until_idle();
    source.cancel(CancelReason::User);
    f.runtime.advance(10s);
    REQUIRE(f.calls == 1);
    CHECK(f.result->error().id == "net.resolve_cancelled");
    CHECK(f.result->error().kind == ErrorKind::Cancelled);

    Fixture answered;
    answered.resolver.set("fast.test", {ip("10.1.1.1")});
    CancelSource late;
    REQUIRE(answered.resolve("fast.test", AddressFamilyPolicy::Any, late.token()));
    late.cancel(CancelReason::User);
    answered.runtime.advance(10s);
    CHECK(answered.calls == 1);
}
