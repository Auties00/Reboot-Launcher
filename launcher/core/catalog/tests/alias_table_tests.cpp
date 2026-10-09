#include <catch2/catch_test_macros.hpp>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "reboot/catalog/alias_table.hpp"
#include "reboot/catalog/catalog.hpp"
#include "reboot/foundation/diag.hpp"

using namespace rb;
using namespace rb::catalog;

namespace {

GameVersion version(std::string_view text) {
    auto parsed = GameVersion::parse(text);
    REQUIRE(parsed);
    return *parsed;
}

CatalogEntry entry(std::string id, std::string_view version_text, std::vector<std::string> aliases = {}) {
    CatalogEntry out;
    out.id = std::move(id);
    out.version = version(version_text);
    out.aliases = std::move(aliases);
    return out;
}

}  // namespace

TEST_CASE("an empty table resolves nothing") {
    const AliasTable table;
    CHECK_FALSE(table.resolve("6.1.1"));
    CHECK_FALSE(table.resolve("cert"));
}

TEST_CASE("a catalog id beats an alias") {
    Catalog catalog;
    catalog.entries = {entry("cert", "3.5", {"3.50.1"}), entry("3.5.1", "3.5.1", {"cert", "4.1"}), entry("4.01", "4.1")};
    const auto table = AliasTable::for_catalog(catalog);

    const auto cert = table.resolve("cert");
    REQUIRE(cert);
    CHECK(cert->entry == "cert");
    CHECK(cert->source == AliasSource::CatalogId);

    const auto alias = table.resolve("3.50.1");
    REQUIRE(alias);
    CHECK(alias->entry == "cert");
    CHECK(alias->source == AliasSource::CatalogAlias);

    const auto alias_only = table.resolve("4.1");
    REQUIRE(alias_only);
    CHECK(alias_only->entry == "3.5.1");
    CHECK(alias_only->source == AliasSource::CatalogAlias);
}

TEST_CASE("resolve ignores ASCII case and surrounding space only") {
    Catalog catalog;
    catalog.entries = {entry("cert", "3.5"), entry("6.1.1", "6.1.1"), entry("6.10.1", "6.10.1")};
    const auto table = AliasTable::for_catalog(catalog);

    CHECK(table.resolve("  CERT\t")->entry == "cert");
    CHECK_FALSE(table.resolve("ce rt"));
    CHECK_FALSE(table.resolve(""));
    CHECK(table.resolve("6.1.1")->version != table.resolve("6.10.1")->version);
}

TEST_CASE("catalog names are trimmed and a blank alias matches nothing") {
    Catalog catalog;
    catalog.entries = {entry("cert", "3.5", {" Season 3 ", "  "})};
    const auto table = AliasTable::for_catalog(catalog);
    CHECK(table.resolve("season 3")->entry == "cert");
    CHECK_FALSE(table.resolve(" "));
}
