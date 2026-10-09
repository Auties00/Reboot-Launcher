#include <catch2/catch_test_macros.hpp>

#include "https_url.hpp"

using rb::os_macos::platform::is_https_url;

TEST_CASE("https links with a host are accepted in any letter case", "[https_url]") {
    CHECK(is_https_url("https://projectreboot.dev/download"));
    CHECK(is_https_url("HTTPS://example.com"));
}

TEST_CASE("other schemes, empty hosts and embedded whitespace are refused", "[https_url]") {
    CHECK_FALSE(is_https_url("http://example.com"));
    CHECK_FALSE(is_https_url("file:///etc/passwd"));
    CHECK_FALSE(is_https_url("https://"));
    CHECK_FALSE(is_https_url("https:///path"));
    CHECK_FALSE(is_https_url("https://exa mple.com"));
    CHECK_FALSE(is_https_url("https://example.com\n"));
    CHECK_FALSE(is_https_url("reboot://00000000-0000-0000-0000-000000000000"));
}
