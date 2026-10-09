#include <string>
#include <string_view>

#include <catch2/catch_test_macros.hpp>

#include "reboot/foundation/text.hpp"

using namespace rb;

TEST_CASE("is_valid_utf8 rejects overlongs, surrogates and truncation", "[foundation][text]") {
    CHECK(is_valid_utf8(""));
    CHECK(is_valid_utf8("plain"));
    CHECK(is_valid_utf8("\xC3\xA9\xE2\x82\xAC\xF0\x9F\x98\x80"));
    CHECK_FALSE(is_valid_utf8("\xC0\xAF"));
    CHECK_FALSE(is_valid_utf8("\xE0\x80\xAF"));
    CHECK_FALSE(is_valid_utf8("\xED\xA0\x80"));
    CHECK_FALSE(is_valid_utf8("\xF4\x90\x80\x80"));
    CHECK_FALSE(is_valid_utf8("\xE2\x82"));
    CHECK_FALSE(is_valid_utf8("\xFF"));
}

TEST_CASE("UTF-8 and UTF-16 convert both ways and replace invalid input", "[foundation][text]") {
    CHECK(utf8_to_utf16("a\xC3\xA9\xF0\x9F\x98\x80") == u"a\u00E9\U0001F600");
    CHECK(utf16_to_utf8(u"a\u00E9\U0001F600") == "a\xC3\xA9\xF0\x9F\x98\x80");
    // One U+FFFD per maximal invalid subpart.
    CHECK(utf8_to_utf16("\xE2\x82x") == u"\uFFFDx");
    CHECK(utf8_to_utf16("\xC0\xAF") == u"\uFFFD\uFFFD");
    const char16_t lone[] = {u'a', 0xD800, u'b', 0xDC00};
    CHECK(utf16_to_utf8(std::u16string_view(lone, 4)) == "a\xEF\xBF\xBD" "b\xEF\xBF\xBD");
}

TEST_CASE("sanitize_display_text strips controls and bidi tricks but keeps joiners", "[foundation][text]") {
    CHECK(sanitize_display_text("a\tb\nc\x7F\x01") == "a\tbc");
    CHECK(sanitize_display_text("evil\xE2\x80\xAEtxt.exe") == "eviltxt.exe");
    CHECK(sanitize_display_text("x\xE2\x81\xA6y\xE2\x81\xA9z") == "xyz");
    CHECK(sanitize_display_text("zero\xE2\x80\x8Bwidth\xEF\xBB\xBF\xE2\x81\xA0") == "zerowidth");
    CHECK(sanitize_display_text("\xC2\x85next") == "next");
    const std::string joiners = "a\xE2\x80\x8D" "b\xE2\x80\x8C" "c\xE2\x80\x8E" "d\xE2\x80\x8F";
    CHECK(sanitize_display_text(joiners) == joiners);
    CHECK(sanitize_display_text("bad\xFF") == "bad\xEF\xBF\xBD");
}

TEST_CASE("sanitize_display_text normalises to NFC", "[foundation][text]") {
    // e + combining acute composes.
    CHECK(sanitize_display_text("e\xCC\x81") == "\xC3\xA9");
    // Already composed stays.
    CHECK(sanitize_display_text("\xC3\xA9") == "\xC3\xA9");
    // Canonical reordering: dot below (220) sorts before acute (230), then both compose onto a.
    CHECK(sanitize_display_text("a\xCC\x81\xCC\xA3") == "\xE1\xBA\xA1\xCC\x81");
    // Singleton decomposition: the Angstrom sign becomes A with ring.
    CHECK(sanitize_display_text("\xE2\x84\xAB") == "\xC3\x85");
    // Hangul L + V + T composes algorithmically.
    CHECK(sanitize_display_text("\xE1\x84\x80\xE1\x85\xA1\xE1\x86\xA8") == "\xEA\xB0\x81");
    // And a precomposed syllable is unchanged.
    CHECK(sanitize_display_text("\xEA\xB0\x81") == "\xEA\xB0\x81");
    // A blocked mark stays apart: two acutes on e, only the first composes.
    CHECK(sanitize_display_text("e\xCC\x81\xCC\x81") == "\xC3\xA9\xCC\x81");
}

TEST_CASE("normalize_detail caps at 2048 bytes on a code point boundary", "[foundation][text]") {
    const std::string ascii(3000, 'x');
    CHECK(normalize_detail(ascii).size() == 2048);
    std::string wide;
    for (int i = 0; i < 1000; ++i) wide += "\xE2\x82\xAC";
    const std::string capped = normalize_detail(wide);
    CHECK(capped.size() == 2046);
    CHECK(is_valid_utf8(capped));
    CHECK(normalize_detail("short") == "short");
}

TEST_CASE("iequals_ascii folds ASCII only", "[foundation][text]") {
    CHECK(iequals_ascii("-AUTH_Password=", "-auth_password="));
    CHECK_FALSE(iequals_ascii("abc", "abcd"));
    CHECK_FALSE(iequals_ascii("\xC3\xA9", "\xC3\x89"));
}
