#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

#include "reboot/os_macos/platform/cf_locale_language_source.hpp"

using rb::os_macos::platform::CfLocaleLanguageSource;

TEST_CASE("the user has at least one preferred language, each a BCP 47 tag", "[cf_locale]") {
    CfLocaleLanguageSource source;
    const std::vector<std::string> tags = source.preferred_ui_languages();
    REQUIRE_FALSE(tags.empty());
    for (const std::string& tag : tags) {
        INFO(tag);
        CHECK(tag.find_first_not_of("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-") == std::string::npos);
    }
}
