#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "reboot/ux/language_info.hpp"
#include "reboot/ux/language_preference.hpp"
#include "reboot/ux/language_tag.hpp"
#include "reboot/ux/resolve_language.hpp"

using namespace rb;
using namespace rb::ux;

namespace {

[[nodiscard]] LanguageTag tag(std::string_view text) {
    Result<LanguageTag> parsed = LanguageTag::parse(text);
    REQUIRE(parsed);
    return *parsed;
}

[[nodiscard]] std::string canonical(std::string_view text) { return tag(text).str(); }

}  // namespace

TEST_CASE("LanguageTag::parse canonicalises case per subtag kind") {
    CHECK(canonical("en") == "en");
    CHECK(canonical("EN") == "en");
    CHECK(canonical("pt-br") == "pt-BR");
    CHECK(canonical("ZH-hant-tw") == "zh-Hant-TW");
    CHECK(canonical("es-419") == "es-419");
    CHECK(canonical("sl-ROZAJ-biske") == "sl-rozaj-biske");
    CHECK(canonical("de-CH-1996") == "de-CH-1996");
    CHECK(canonical("zh-min-nan") == "zh-min-nan");
    CHECK(canonical("en-US-u-ca-GREGORY") == "en-US-u-ca-gregory");
    CHECK(canonical("en-a-bbb-x-A-CCC") == "en-a-bbb-x-a-ccc");
    CHECK(canonical("X-Private") == "x-private");
    CHECK(canonical("I-KLINGON") == "i-klingon");
    CHECK(canonical("en-gb-OED") == "en-GB-oed");
}

TEST_CASE("LanguageTag::parse rejects ill-formed tags") {
    for (const std::string_view bad :
         {"", "e", "en_US", "en-", "-en", "en--US", "en-US-", "123", "en-toolongsubtag", "en-u", "en-u-a", "x",
          "en-x", "en-x-toolongsub", "en-US-u-ca-x", "de-1996-1996", "en-a-bbb-a-ccc", "en US", "de-DE.UTF-8", "i-foo",
          "en-Latn-Latn"}) {
        INFO(std::string(bad));
        const Result<LanguageTag> parsed = LanguageTag::parse(bad);
        REQUIRE_FALSE(parsed);
        CHECK(parsed.error().id == "ux.malformed_language_tag");
        CHECK(parsed.error().kind == ErrorKind::InvalidInput);
        CHECK(parsed.error().find_arg("value") != nullptr);
    }
}

TEST_CASE("LanguageTag::primary_language is the first subtag") {
    CHECK(tag("zh-Hant-TW").primary_language() == "zh");
    CHECK(tag("en").primary_language() == "en");
}

TEST_CASE("LanguageTag::from_os_locale accepts POSIX and Windows forms") {
    CHECK(LanguageTag::from_os_locale("de_DE.UTF-8")->str() == "de-DE");
    CHECK(LanguageTag::from_os_locale("de-DE")->str() == "de-DE");
    CHECK(LanguageTag::from_os_locale("pt_BR")->str() == "pt-BR");
    CHECK(LanguageTag::from_os_locale("fr")->str() == "fr");
    CHECK(LanguageTag::from_os_locale("sr_RS@latin")->str() == "sr-Latn-RS");
    CHECK(LanguageTag::from_os_locale("sr_RS.UTF-8@latin")->str() == "sr-Latn-RS");
    CHECK(LanguageTag::from_os_locale("uz_UZ@cyrillic")->str() == "uz-Cyrl-UZ");
    CHECK(LanguageTag::from_os_locale("ca_ES@valencia")->str() == "ca-ES-valencia");
    CHECK(LanguageTag::from_os_locale("de_DE@euro")->str() == "de-DE");
    CHECK(LanguageTag::from_os_locale("zh-Hans-CN")->str() == "zh-Hans-CN");
    CHECK(LanguageTag::from_os_locale("zh_Hant_TW@latin")->str() == "zh-Hant-TW");
}

TEST_CASE("LanguageTag::from_os_locale gives nullopt for C, POSIX and junk") {
    CHECK_FALSE(LanguageTag::from_os_locale("C"));
    CHECK_FALSE(LanguageTag::from_os_locale("C.UTF-8"));
    CHECK_FALSE(LanguageTag::from_os_locale("POSIX"));
    CHECK_FALSE(LanguageTag::from_os_locale(""));
    CHECK_FALSE(LanguageTag::from_os_locale(".UTF-8"));
    CHECK_FALSE(LanguageTag::from_os_locale("not a locale"));
}

TEST_CASE("LanguagePreference round-trips system and explicit tags") {
    const Result<LanguagePreference> system = LanguagePreference::parse("system");
    REQUIRE(system);
    CHECK(system->follows_system());
    CHECK(system->to_setting_value() == "system");
    CHECK(*system == LanguagePreference{});

    const Result<LanguagePreference> german = LanguagePreference::parse("de-at");
    REQUIRE(german);
    CHECK_FALSE(german->follows_system());
    CHECK(german->tag()->str() == "de-AT");
    CHECK(german->to_setting_value() == "de-AT");

    const Result<LanguagePreference> bad = LanguagePreference::parse("de_AT");
    REQUIRE_FALSE(bad);
    CHECK(bad.error().id == "ux.malformed_language_tag");
    CHECK_FALSE(LanguagePreference::parse(""));

    const Result<LanguagePreference> capitalised = LanguagePreference::parse("System");
    REQUIRE(capitalised);
    CHECK(capitalised->follows_system());
}

TEST_CASE("is_rtl uses the script subtag before the primary language") {
    CHECK(is_rtl(tag("ar")));
    CHECK(is_rtl(tag("he-IL")));
    CHECK(is_rtl(tag("fa")));
    CHECK(is_rtl(tag("ur-PK")));
    CHECK(is_rtl(tag("pa-Arab")));
    CHECK(is_rtl(tag("ff-Adlm")));
    CHECK_FALSE(is_rtl(tag("en")));
    CHECK_FALSE(is_rtl(tag("az-Latn")));
    CHECK_FALSE(is_rtl(tag("ar-Latn")));
    CHECK_FALSE(is_rtl(tag("de-a-arab")));
}

TEST_CASE("shipped_languages is English only and describe_language flags unshipped tags") {
    REQUIRE(shipped_languages().size() == 1);
    CHECK(shipped_languages()[0].tag == LanguageTag::english());
    CHECK_FALSE(shipped_languages()[0].rtl);

    const LanguageInfo us = describe_language(tag("en-US"));
    CHECK(us.tag.str() == "en-US");
    CHECK(us.shipped);
    const LanguageInfo arabic = describe_language(tag("ar-EG"));
    CHECK(arabic.rtl);
    CHECK_FALSE(arabic.shipped);
}

TEST_CASE("resolve_language does RFC 4647 Lookup over explicit, OS, then en") {
    const std::vector<LanguageInfo> shipped{
        LanguageInfo{.tag = LanguageTag::english()},
        LanguageInfo{.tag = tag("de")},
        LanguageInfo{.tag = tag("pt-BR")},
        LanguageInfo{.tag = tag("ar"), .rtl = true},
    };

    SECTION("an explicit tag truncates to a shipped one") {
        const ResolvedLanguage r = resolve_language(LanguagePreference(tag("de-AT-1996")), {}, shipped);
        CHECK(r.tag.str() == "de");
        CHECK(r.source == LanguageSource::Explicit);
        CHECK_FALSE(r.unavailable_explicit);
    }
    SECTION("a singleton left last is truncated with its subtag") {
        const ResolvedLanguage r = resolve_language(LanguagePreference(tag("de-a-foo")), {}, shipped);
        CHECK(r.tag.str() == "de");
    }
    SECTION("Lookup never widens a range") {
        const std::vector<LanguageTag> os{tag("pt")};
        const ResolvedLanguage r = resolve_language(LanguagePreference{}, os, shipped);
        CHECK(r.tag.str() == "en");
        CHECK(r.source == LanguageSource::Default);
    }
    SECTION("an unshipped explicit tag is kept and the OS list is tried next") {
        const std::vector<LanguageTag> os{tag("fr-FR"), tag("pt-BR"), tag("de")};
        const ResolvedLanguage r = resolve_language(LanguagePreference(tag("ja")), os, shipped);
        CHECK(r.tag.str() == "pt-BR");
        CHECK(r.source == LanguageSource::Os);
        REQUIRE(r.unavailable_explicit);
        CHECK(r.unavailable_explicit->str() == "ja");
    }
    SECTION("rtl comes from the shipped language") {
        const std::vector<LanguageTag> os{tag("ar-EG")};
        const ResolvedLanguage r = resolve_language(LanguagePreference{}, os, shipped);
        CHECK(r.tag.str() == "ar");
        CHECK(r.rtl);
    }
    SECTION("nothing matches: en by default") {
        const std::vector<LanguageTag> os{tag("fr")};
        const ResolvedLanguage r = resolve_language(LanguagePreference(tag("ja")), os, shipped);
        CHECK(r.tag == LanguageTag::english());
        CHECK(r.source == LanguageSource::Default);
        CHECK_FALSE(r.rtl);
        CHECK(r.unavailable_explicit->str() == "ja");
    }
    SECTION("v1's shipped set always ends at en") {
        const std::vector<LanguageTag> os{tag("de-DE")};
        const ResolvedLanguage r = resolve_language(LanguagePreference{}, os, shipped_languages());
        CHECK(r.tag == LanguageTag::english());
        CHECK(r.source == LanguageSource::Default);
    }
}
