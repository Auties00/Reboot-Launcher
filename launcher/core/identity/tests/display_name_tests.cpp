#include <catch2/catch_test_macros.hpp>
#include <string>

#include "messages.hpp"
#include "reboot/identity/display_name.hpp"
#include "reboot/testing/fake_random.hpp"

using namespace rb;
using namespace rb::identity;

TEST_CASE("validate_display_name accepts 3 to 16 ASCII letters and digits", "[identity]") {
    CHECK(validate_display_name("Bob").has_value());
    CHECK(validate_display_name("Player123456").has_value());
    CHECK(validate_display_name("abcdefghijklmnop").has_value());
}

TEST_CASE("validate_display_name reports each failure with its own id", "[identity]") {
    const auto too_short = validate_display_name("ab");
    REQUIRE_FALSE(too_short);
    CHECK(too_short.error().is(msg::kDisplayNameTooShort));

    const auto too_long = validate_display_name("abcdefghijklmnopq");
    REQUIRE_FALSE(too_long);
    CHECK(too_long.error().is(msg::kDisplayNameTooLong));

    for (const char* name : {"John Doe", "John-Doe", "J\xC3\xB6hn", "john@x"}) {
        const auto invalid = validate_display_name(name);
        REQUIRE_FALSE(invalid);
        CHECK(invalid.error().is(msg::kDisplayNameInvalidCharacter));
    }
}

TEST_CASE("default names are Player and 6 digits", "[identity]") {
    testing::FakeRandom random;
    const std::string name = default_display_name(random);
    CHECK(name.starts_with("Player"));
    CHECK(validate_display_name(name).has_value());
    CHECK(is_default_display_name(name));
    CHECK_FALSE(is_default_display_name("Host123456"));
}

TEST_CASE("is_default_display_name needs exactly 6 digits", "[identity]") {
    CHECK(is_default_display_name("Player000000"));
    CHECK_FALSE(is_default_display_name("Player"));
    CHECK_FALSE(is_default_display_name("Player12345"));
    CHECK_FALSE(is_default_display_name("Player1234567"));
    CHECK_FALSE(is_default_display_name("Player12345a"));
    CHECK_FALSE(is_default_display_name("player123456"));
}
