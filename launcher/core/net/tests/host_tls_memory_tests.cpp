#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "reboot/net/host_tls_memory.hpp"

using namespace reboot;
using namespace reboot::net;

namespace {

struct Fixture {
    explicit Fixture(std::vector<storage::UpstreamTlsMemory> loaded = {})
        : memory(std::move(loaded), [this](std::vector<storage::UpstreamTlsMemory> records) { saved.push_back(std::move(records)); }) {}

    std::vector<std::vector<storage::UpstreamTlsMemory>> saved;
    HostTlsMemory memory;
};

}  // namespace

TEST_CASE("https is always allowed; plain http needs a remembered answer", "[net][tls]") {
    Fixture f;
    CHECK(f.memory.check(UrlScheme::Https, "new.test"));
    const Result<void> plain = f.memory.check(UrlScheme::Http, "new.test");
    REQUIRE_FALSE(plain);
    CHECK(plain.error().id == "net.plain_http_needs_consent");

    f.memory.remember_http_acknowledged("New.Test:8080");
    CHECK(f.memory.check(UrlScheme::Http, "new.test"));
    CHECK(f.memory.check(UrlScheme::Http, "NEW.test:80"));
    REQUIRE(f.saved.size() == 1);
    CHECK(f.saved.back() == std::vector<storage::UpstreamTlsMemory>{{"new.test", false, true}});
}

TEST_CASE("a host once seen on https refuses http even after consent", "[net][tls]") {
    Fixture f;
    f.memory.remember_http_acknowledged("mixed.test");
    f.memory.remember_https("mixed.test");
    const Result<void> plain = f.memory.check(UrlScheme::Http, "mixed.test");
    REQUIRE_FALSE(plain);
    CHECK(plain.error().id == "net.https_downgrade_refused");
    CHECK(f.saved.size() == 2);
    f.memory.remember_https("mixed.test");
    CHECK(f.saved.size() == 2);
}

TEST_CASE("only literal loopback hosts are exempt and never stored", "[net][tls]") {
    Fixture f;
    for (const char* host : {"localhost", "127.0.0.1", "127.8.9.10", "::1", "[::1]:3551", "LOCALHOST"})
        CHECK(f.memory.check(UrlScheme::Http, host));
    CHECK_FALSE(f.memory.check(UrlScheme::Http, "localhost.example"));
    CHECK_FALSE(f.memory.check(UrlScheme::Http, "128.0.0.1"));
    f.memory.remember_https("127.0.0.1");
    CHECK(f.memory.check(UrlScheme::Http, "127.0.0.1"));
    CHECK(f.saved.empty());
}

TEST_CASE("loaded records are normalised and merged", "[net][tls]") {
    Fixture f({{"A.test", true, false}, {"a.test", false, true}, {"b.test", false, true}});
    CHECK(f.memory.check(UrlScheme::Http, "a.test").error().id == "net.https_downgrade_refused");
    CHECK(f.memory.check(UrlScheme::Http, "b.test"));
    const std::vector<storage::UpstreamTlsMemory> records = f.memory.records();
    REQUIRE(records.size() == 2);
    CHECK(records[0] == storage::UpstreamTlsMemory{"a.test", true, true});
    CHECK(records[1] == storage::UpstreamTlsMemory{"b.test", false, true});
    CHECK(f.saved.empty());
}
