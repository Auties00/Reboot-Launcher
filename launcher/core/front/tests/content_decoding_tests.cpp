#include <catch2/catch_test_macros.hpp>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "content_decoding.hpp"
#include "front_test_support.hpp"

using namespace reboot;
using namespace reboot::front;

namespace {

constexpr std::string_view kText = "[OnlineSubsystemMcp.Xmpp]\nServerAddr=\"ws://xmpp.example:443\"\n";

std::span<const u8> bytes(std::string_view text) { return {reinterpret_cast<const u8*>(text.data()), text.size()}; }

std::string text(const std::vector<u8>& data) { return {data.begin(), data.end()}; }

}  // namespace

TEST_CASE("identity bodies are taken as they are", "[front][decode]") {
    CHECK(text(*decode_content("", bytes(kText), 1024)) == kText);
    CHECK(text(*decode_content(" Identity ", bytes(kText), 1024)) == kText);
    CHECK(!decode_content("identity", bytes(kText), 8));
}

TEST_CASE("gzip, zlib and raw deflate bodies are inflated", "[front][decode]") {
    const std::vector<u8> gzipped = test::gzip(kText);
    CHECK(text(*decode_content("gzip", gzipped, 1024)) == kText);
    CHECK(text(*decode_content("x-gzip", gzipped, 1024)) == kText);

    const std::vector<u8> raw = test::deflate_raw(kText);
    CHECK(text(*decode_content("deflate", raw, 1024)) == kText);
    std::vector<u8> zlib{0x78, 0x9C};
    zlib.insert(zlib.end(), raw.begin(), raw.end());
    zlib.insert(zlib.end(), {0, 0, 0, 0});
    CHECK(text(*decode_content("deflate", zlib, 1024)) == kText);
}

TEST_CASE("unknown codings, corrupt or truncated data and bombs are not learned from", "[front][decode]") {
    CHECK(!decode_content("br", bytes(kText), 1024));
    CHECK(!decode_content("gzip, deflate", test::gzip(kText), 1024));
    CHECK(!decode_content("gzip", bytes(kText), 1024));

    std::vector<u8> truncated = test::gzip(kText);
    truncated.resize(truncated.size() - 20);
    CHECK(!decode_content("gzip", truncated, 1024));

    const std::string zeros(64 * 1024, '\0');
    CHECK(decode_content("gzip", test::gzip(zeros), zeros.size()));
    CHECK(!decode_content("gzip", test::gzip(zeros), zeros.size() - 1));
}
