#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "redirect_guard.hpp"

using namespace rb;
using namespace rb::net;

TEST_CASE("a plain http hop reaches only the hosts HttpClient allowed", "[net][tls][redirect]") {
    RedirectGuard guard({"Origin.test:8080", "acked.test"});
    CHECK_FALSE(guard.admit("http://origin.test:8080/a"));
    CHECK_FALSE(guard.admit("http://ACKED.test/b"));
    const auto other = guard.admit("http://other.test/c");
    REQUIRE(other);
    CHECK(other->code == HttpErrorCode::PlainHttpNeedsConsent);
    CHECK(other->host == "other.test");
    CHECK(to_diagnostic(*other).id == "net.plain_http_needs_consent");
    // https needs no consent.
    CHECK_FALSE(guard.admit("https://other.test/d"));
}

TEST_CASE("a host the chain reached over https is never reached over http again", "[net][tls][redirect]") {
    RedirectGuard guard({"origin.test"});
    CHECK_FALSE(guard.admit("http://origin.test/"));
    CHECK_FALSE(guard.admit("https://origin.test/secure"));
    const auto back = guard.admit("http://origin.test/plain");
    REQUIRE(back);
    CHECK(back->code == HttpErrorCode::DowngradeRefused);
    CHECK(to_diagnostic(*back).id == "net.https_downgrade_refused");
}

TEST_CASE("literal loopback hops are exempt, as in HostTlsMemory", "[net][tls][redirect]") {
    RedirectGuard guard({});
    CHECK_FALSE(guard.admit("https://127.0.0.1:4433/"));
    for (const char* url : {"http://localhost:8080/", "http://127.0.0.1:4433/", "http://[::1]:80/"})
        CHECK_FALSE(guard.admit(url));
    CHECK(guard.admit("http://localhost.example/"));
}

TEST_CASE("a plain hop to a host seen on https before is a downgrade, not a consent question", "[net][tls][redirect]") {
    RedirectGuard guard({"origin.test"}, {"Secure.test"});
    CHECK_FALSE(guard.admit("http://origin.test/"));
    const auto back = guard.admit("http://secure.test/plain");
    REQUIRE(back);
    CHECK(back->code == HttpErrorCode::DowngradeRefused);
}

TEST_CASE("an unparseable hop is reported without its user info, path or query", "[net][tls][redirect]") {
    RedirectGuard guard({"origin.test"});
    const auto refused = guard.admit("http://user:secret@other.test/path?token=abc");
    REQUIRE(refused);
    CHECK(refused->code == HttpErrorCode::InvalidUrl);
    CHECK(refused->host == "http://other.test");
}
