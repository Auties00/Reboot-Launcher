#include <catch2/catch_test_macros.hpp>

#include "reboot/publish/share_link.hpp"

using namespace rb;
using namespace rb::publish;

TEST_CASE("a share link is the lowercase scheme and the server id", "[publish]") {
    ServerId server;
    for (u8 i = 0; i < 16; ++i) server.value.bytes[i] = static_cast<u8>(0xA0 + i);

    CHECK(ShareLink{server}.url() == "reboot://a0a1a2a3-a4a5-a6a7-a8a9-aaabacadaeaf");
}
