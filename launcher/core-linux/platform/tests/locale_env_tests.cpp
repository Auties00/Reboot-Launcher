#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

#include "locale_env.hpp"

using namespace rb::os_linux::platform;

using Tags = std::vector<std::string>;

TEST_CASE("the messages locale is the first of LC_ALL, LC_MESSAGES and LANG that is set", "[locale]") {
    CHECK(preferred_languages({.lang = "de_DE.UTF-8"}) == Tags{"de_DE.UTF-8"});
    CHECK(preferred_languages({.lc_messages = "fr_FR.UTF-8", .lang = "de_DE.UTF-8"}) == Tags{"fr_FR.UTF-8"});
    CHECK(preferred_languages({.lc_all = "it_IT", .lc_messages = "fr_FR", .lang = "de_DE"}) == Tags{"it_IT"});
    CHECK(preferred_languages({.lc_all = "", .lang = "de_DE"}) == Tags{"de_DE"});
    CHECK(preferred_languages({}).empty());
}

TEST_CASE("LANGUAGE's list comes first, without C entries or repeats", "[locale]") {
    CHECK(preferred_languages({.language = "pt_BR:fr::C:pt_BR", .lang = "de_DE.UTF-8"}) ==
          Tags{"pt_BR", "fr", "de_DE.UTF-8"});
    CHECK(preferred_languages({.language = "de_DE.UTF-8", .lang = "de_DE.UTF-8"}) == Tags{"de_DE.UTF-8"});
}

TEST_CASE("a C or POSIX messages locale gives no language, LANGUAGE included", "[locale]") {
    CHECK(preferred_languages({.language = "de", .lang = "C"}).empty());
    CHECK(preferred_languages({.language = "de", .lc_all = "C.UTF-8", .lang = "fr_FR"}).empty());
    CHECK(preferred_languages({.lang = "POSIX"}).empty());
}
