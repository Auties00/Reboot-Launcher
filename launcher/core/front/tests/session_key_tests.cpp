#include <catch2/catch_test_macros.hpp>
#include <functional>

#include "reboot/front/session_key.hpp"
#include "reboot/testing/fake_random.hpp"

using namespace rb;
using namespace rb::front;

TEST_CASE("a session key round-trips through lowercase hex", "[front][key]") {
    testing::FakeRandom random(7);
    const SessionKey key = SessionKey::generate(random);
    const auto hex = key.to_hex();
    CHECK(hex.size() == 32);
    const auto parsed = SessionKey::parse(hex);
    REQUIRE(parsed.has_value());
    CHECK(*parsed == key);
    CHECK(std::hash<SessionKey>{}(*parsed) == std::hash<SessionKey>{}(key));
}

TEST_CASE("generated keys differ", "[front][key]") {
    testing::FakeRandom random(7);
    CHECK(!(SessionKey::generate(random) == SessionKey::generate(random)));
}

TEST_CASE("only exactly 32 hex digits parse", "[front][key]") {
    CHECK(SessionKey::parse("FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF").has_value());
    CHECK(!SessionKey::parse("fffffffffffffffffffffffffffffff"));
    CHECK(!SessionKey::parse("fffffffffffffffffffffffffffffffff"));
    CHECK(!SessionKey::parse("fffffffffffffffffffffffffffffffz"));
}
