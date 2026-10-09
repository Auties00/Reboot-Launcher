#include <catch2/catch_test_macros.hpp>

#include "reboot/backend/backend_url.hpp"

using namespace rb;
using namespace rb::backend;

TEST_CASE("a bare host gets the default port and no scheme", "[backend]") {
    const Result<BackendUrl> url = BackendUrl::parse("Example.COM");
    REQUIRE(url);
    CHECK_FALSE(url->scheme);
    CHECK(url->host == "example.com");
    CHECK(url->port == kDefaultBackendPort);
    CHECK(url->origin() == "https://example.com:3551");
}

TEST_CASE("a scheme, a port and a trailing slash are kept", "[backend]") {
    const Result<BackendUrl> url = BackendUrl::parse("HTTP://10.0.0.2:8080/");
    REQUIRE(url);
    CHECK(url->scheme == net::UrlScheme::Http);
    CHECK(url->host == "10.0.0.2");
    CHECK(url->port == Port{8080});
    CHECK(url->origin() == "http://10.0.0.2:8080");
}

TEST_CASE("an IPv6 origin is bracketed", "[backend]") {
    const Result<BackendUrl> url = BackendUrl::parse("https://[::1]:3551");
    REQUIRE(url);
    CHECK(url->origin() == "https://[::1]:3551");
}

TEST_CASE("a path, a query or user info is not a backend address", "[backend]") {
    for (const char* text : {"example.com/api", "example.com?x=1", "user@example.com", "http://a.b/c/"}) {
        const Result<BackendUrl> url = BackendUrl::parse(text);
        REQUIRE_FALSE(url);
        CHECK(url.error().id == "backend.invalid_url");
    }
}

TEST_CASE("port 0 is refused", "[backend]") {
    const Result<BackendUrl> url = BackendUrl::parse("example.com:0");
    REQUIRE_FALSE(url);
    CHECK(url.error().id == "backend.invalid_port");
}
