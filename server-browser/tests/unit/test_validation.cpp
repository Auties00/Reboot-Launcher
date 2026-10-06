#include <catch2/catch_test_macros.hpp>

#include "registry/validation.hpp"

using namespace sb;
using namespace sb::registry;

TEST_CASE("text validation enforces strict UTF-8 without control characters", "[validation]") {
    CHECK(valid_text("Late Game", 64, false));
    CHECK(valid_text("Caf\xC3\xA9 \xF0\x9F\x8E\xAE", 64, false));
    CHECK_FALSE(valid_text("", 64, false));
    CHECK(valid_text("", 64, true));
    CHECK_FALSE(valid_text("tab\there", 64, false));
    CHECK_FALSE(valid_text("\x7F", 64, false));
    CHECK_FALSE(valid_text("\xC0\xAF", 64, false));          // overlong slash
    CHECK_FALSE(valid_text("\xED\xA0\x80", 64, false));      // surrogate
    CHECK_FALSE(valid_text("\xF4\x90\x80\x80", 64, false));  // above U+10FFFF
    CHECK_FALSE(valid_text("\xC2\x85", 64, false));          // C1 control (NEL)
    CHECK_FALSE(valid_text("\xE2\x82", 64, false));          // truncated sequence
    CHECK_FALSE(valid_text(std::string(65, 'a'), 64, false));
}

TEST_CASE("versions map to stable buckets", "[validation]") {
    CHECK(version_bucket("4.5") == 2 + 4 * 1024 + 5);
    CHECK(version_bucket("4.50") == 2 + 4 * 1024 + 50);
    CHECK(version_bucket("1.7.2") == 2 + 1 * 1024 + 7);
    CHECK(version_bucket("34.10") == 2 + 34 * 1024 + 10);
    CHECK(version_bucket("5") == 2 + 5 * 1024);
    CHECK(version_bucket("Cert") == wire::kBucketOther);
    CHECK(version_bucket("") == wire::kBucketOther);
    CHECK(version_bucket("99999.1") == wire::kBucketOther);
}

TEST_CASE("host registration validation", "[validation]") {
    wire::HostRegister r{.name = "ok", .version = "4.5", .game_port = 7777, .max_players = 100};
    r.id.bytes[0] = 1;
    FieldLimits lim;
    CHECK_FALSE(validate_register(r, lim));
    auto bad = r;
    bad.game_port = 0;
    CHECK(validate_register(bad, lim)->field == std::string_view("game_port"));
    bad = r;
    bad.id = {};
    CHECK(validate_register(bad, lim)->field == std::string_view("id"));
    bad = r;
    bad.name = std::string(65, 'n');
    CHECK(validate_register(bad, lim)->field == std::string_view("name"));
}
