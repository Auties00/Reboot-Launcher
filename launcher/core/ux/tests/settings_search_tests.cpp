#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "reboot/foundation/diag.hpp"
#include "reboot/ux/message_catalog.hpp"
#include "reboot/ux/settings_search.hpp"

using namespace rb;
using namespace rb::ux;

namespace {

REBOOT_MESSAGE(kListingTitle, "ux.test_search_listing", "Listed in server browser");
REBOOT_MESSAGE(kListingDescription, "ux.test_search_listing_description", "Anyone can find a listed server");
REBOOT_MESSAGE(kNameTitle, "ux.test_search_name", "Server name");
REBOOT_MESSAGE(kNameDescription, "ux.test_search_name_description", "Shown in the server browser");
REBOOT_MESSAGE(kLanguageTitle, "ux.test_search_language", "Language");
REBOOT_MESSAGE(kConsoleTitle, "ux.test_search_console", "Console key");
REBOOT_MESSAGE(kConsoleDescription, "ux.test_search_console_description", "The key that opens the game console");
REBOOT_MESSAGE(kGrosseTitle, "ux.test_search_grosse", "Fenstergr\xc3\xb6\xc3\x9f" "e");

[[nodiscard]] SettingsSearch make_search() {
    return SettingsSearch({
        {"host.listing", "host", kListingTitle, kListingDescription},
        {"host.name", "host", kNameTitle, kNameDescription},
        {"ui.language", "ui", kLanguageTitle, std::nullopt},
        {"game.console_key", "game", kConsoleTitle, kConsoleDescription},
        {"backend.port", "backend", MessageId{"ux.test_search_untranslated"}, MessageId{"ux.test_search_missing"}},
        {"ui.window_size", "ui", kGrosseTitle, std::nullopt},
    });
}

[[nodiscard]] std::vector<std::string> keys(const std::vector<SettingMatch>& matches) {
    std::vector<std::string> out;
    for (const SettingMatch& match : matches) out.push_back(match.key);
    return out;
}

}  // namespace

TEST_CASE("SettingsSearch matches nothing for an empty query or limit") {
    const SettingsSearch search = make_search();
    const MessageCatalog catalog = MessageCatalog::english_from_registry();
    CHECK(search.search("", catalog, 10).empty());
    CHECK(search.search("   \t", catalog, 10).empty());
    CHECK(search.search("..", catalog, 10).empty());
    CHECK(search.search("server", catalog, 0).empty());
    CHECK(search.search("zzz", catalog, 10).empty());
}

TEST_CASE("SettingsSearch ranks title over key over description, ties in registry order") {
    const SettingsSearch search = make_search();
    const MessageCatalog catalog = MessageCatalog::english_from_registry();

    const std::vector<SettingMatch> server = search.search("server", catalog, 10);
    CHECK(keys(server) == std::vector<std::string>{"host.name", "host.listing"});
    CHECK(server[0].field == MatchField::Title);
    CHECK(server[0].group == "host");
    CHECK(server[0].score > server[1].score);

    const std::vector<SettingMatch> host = search.search("host", catalog, 10);
    CHECK(keys(host) == std::vector<std::string>{"host.listing", "host.name"});
    CHECK(host[0].field == MatchField::Key);
    CHECK(host[0].score == host[1].score);

    const std::vector<SettingMatch> lang = search.search("lang", catalog, 10);
    REQUIRE(lang.size() == 1);
    CHECK(lang[0].key == "ui.language");
    CHECK(lang[0].field == MatchField::Title);

    const std::vector<SettingMatch> opens = search.search("opens", catalog, 10);
    REQUIRE(opens.size() == 1);
    CHECK(opens[0].field == MatchField::Description);
    CHECK(opens[0].score < lang[0].score);
}

TEST_CASE("SettingsSearch tokenises keys on dots and underscores") {
    const SettingsSearch search = make_search();
    const MessageCatalog catalog = MessageCatalog::english_from_registry();

    const std::vector<SettingMatch> dotted = search.search("host.na", catalog, 10);
    REQUIRE(dotted.size() == 1);
    CHECK(dotted[0].key == "host.name");
    CHECK(dotted[0].field == MatchField::Key);

    const std::vector<SettingMatch> both = search.search("console GAME", catalog, 10);
    REQUIRE(both.size() == 1);
    CHECK(both[0].key == "game.console_key");
    CHECK(both[0].field == MatchField::Key);
}

TEST_CASE("SettingsSearch needs every query token in one field") {
    const SettingsSearch search = make_search();
    const MessageCatalog catalog = MessageCatalog::english_from_registry();
    CHECK(search.search("listed name", catalog, 10).empty());
    CHECK(keys(search.search("LISTED Browser", catalog, 10)) == std::vector<std::string>{"host.listing"});
}

TEST_CASE("SettingsSearch prefers whole-token and leading matches") {
    const SettingsSearch search = make_search();
    const MessageCatalog catalog = MessageCatalog::english_from_registry();
    const std::vector<SettingMatch> exact = search.search("key", catalog, 10);
    const std::vector<SettingMatch> prefix = search.search("ke", catalog, 10);
    REQUIRE(exact.size() == 1);
    REQUIRE(prefix.size() == 1);
    CHECK(exact[0].score > prefix[0].score);
}

TEST_CASE("SettingsSearch truncates to the limit after ranking") {
    const SettingsSearch search = make_search();
    const MessageCatalog catalog = MessageCatalog::english_from_registry();
    CHECK(keys(search.search("server", catalog, 1)) == std::vector<std::string>{"host.name"});
}

TEST_CASE("SettingsSearch falls back to the key when the catalog lacks a title") {
    const SettingsSearch search = make_search();
    const MessageCatalog catalog = MessageCatalog::english_from_registry();
    const std::vector<SettingMatch> port = search.search("port", catalog, 10);
    REQUIRE(port.size() == 1);
    CHECK(port[0].key == "backend.port");
    CHECK(port[0].field == MatchField::Key);
}

TEST_CASE("SettingsSearch keeps non-ASCII bytes inside tokens") {
    const SettingsSearch search = make_search();
    const MessageCatalog catalog = MessageCatalog::english_from_registry();
    CHECK(keys(search.search("fenstergr\xc3\xb6", catalog, 10)) == std::vector<std::string>{"ui.window_size"});
}
