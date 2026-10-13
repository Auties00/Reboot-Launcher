#include <catch2/catch_test_macros.hpp>

#include <cctype>
#include <string>
#include <vector>

#include "reboot/os_windows/platform/mui_language_source.hpp"

using rb::os_windows::platform::MuiLanguageSource;

TEST_CASE("the user has at least one UI language, each a BCP 47 tag", "[mui_language]") {
    MuiLanguageSource source;
    const std::vector<std::string> tags = source.preferred_ui_languages();
    REQUIRE_FALSE(tags.empty());
    for (const std::string& tag : tags) {
        INFO(tag);
        CHECK(std::isalpha(static_cast<unsigned char>(tag.front())) != 0);
        CHECK(tag.find_first_not_of("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-") == std::string::npos);
    }
}
