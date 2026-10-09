#include <catch2/catch_test_macros.hpp>

#include "reboot/integration/openable_url.hpp"

using rb::integration::is_openable_url;

TEST_CASE("absolute https links with a host open", "[integration][openable_url]") {
    CHECK(is_openable_url("https://github.com/Auties00/reboot_launcher/issues/new"));
    CHECK(is_openable_url("HTTPS://discord.gg/rebootmp"));
    CHECK(is_openable_url("https://example.com:8443?x=1"));
}

TEST_CASE("other schemes and hostless links are refused", "[integration][openable_url]") {
    CHECK_FALSE(is_openable_url("http://example.com"));
    CHECK_FALSE(is_openable_url("file:///etc/passwd"));
    CHECK_FALSE(is_openable_url("reboot://00000000-0000-0000-0000-000000000000"));
    CHECK_FALSE(is_openable_url("https://"));
    CHECK_FALSE(is_openable_url("https:///path"));
    CHECK_FALSE(is_openable_url("https://user@/path"));
    CHECK_FALSE(is_openable_url("https://:443/"));
}

TEST_CASE("whitespace and control characters are refused", "[integration][openable_url]") {
    CHECK_FALSE(is_openable_url("https://example.com/a b"));
    CHECK_FALSE(is_openable_url("https://example.com/\n"));
    CHECK_FALSE(is_openable_url("https://example.com/\x7f"));
}
