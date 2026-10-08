#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>
#include <vector>

#include "reboot/catalog/catalog.hpp"
#include "reboot/catalog/catalog_error.hpp"
#include "reboot/catalog/parse_catalog.hpp"
#include "reboot/foundation/diag.hpp"

using namespace reboot;
using namespace reboot::catalog;

namespace {

std::vector<u8> read_data(std::string_view name) {
    std::ifstream stream(std::string(REBOOT_CATALOG_TEST_DATA) + "/" + std::string(name), std::ios::binary);
    REQUIRE(stream);
    return {std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
}

std::vector<u8> bytes(std::string_view text) { return {text.begin(), text.end()}; }

GameVersion version(std::string_view text) {
    auto parsed = GameVersion::parse(text);
    REQUIRE(parsed);
    return *parsed;
}

// One range over every version, so `entries` alone decides the outcome.
std::string document(std::string_view entries, std::string_view ranges = R"([{"first":"0.0","last":"4095.1023"}])") {
    return R"({"schema":1,"serial":1,"generated_unix_ms":0,"expires_unix_ms":0,"entries":)" + std::string(entries) +
           R"(,"flag_ranges":)" + std::string(ranges) + "}";
}

std::string entry(std::string_view id, std::string_view version_text, std::string_view extra = "") {
    return R"({"id":")" + std::string(id) + R"(","version":")" + std::string(version_text) +
           R"(","url":"https://builds.example/a.zip","format":"zip","container":"none","archive_size":1,"availability":"available")" +
           std::string(extra) + "}";
}

CatalogError parse_error(const std::string& json) {
    auto parsed = parse_catalog(bytes(json));
    REQUIRE_FALSE(parsed);
    return parsed.error();
}

}  // namespace

TEST_CASE("parse_catalog reads the golden document") {
    const auto catalog = parse_catalog(read_data("catalog.json"));
    REQUIRE(catalog);
    CHECK(catalog->schema == kCatalogSchema);
    CHECK(catalog->serial == 42);
    CHECK(catalog->generated_at.time_since_epoch() == std::chrono::milliseconds(1790000000000));
    CHECK(catalog->expires_at.time_since_epoch() == std::chrono::milliseconds(1792592000000));
    REQUIRE(catalog->entries.size() == 4);
    REQUIRE(catalog->flag_ranges.size() == 3);

    const CatalogEntry* latest = catalog->find("12.41");
    REQUIRE(latest != nullptr);
    CHECK(latest->changelist == Changelist{12905909});
    CHECK(latest->display_name == "Fortnite 12.41");
    CHECK(latest->format == ArchiveFormat::SevenZip);
    CHECK(latest->container == ArchiveContainer::ZipStored);
    CHECK(latest->archive_size == 24000000000);
    CHECK(latest->installed_size == 61000000000);
    REQUIRE(latest->sha256);
    CHECK((*latest->sha256)[0] == 0x00);
    CHECK((*latest->sha256)[1] == 0x11);
    CHECK((*latest->sha256)[31] == 0xff);

    const CatalogEntry* cert = catalog->find("cert");
    REQUIRE(cert != nullptr);
    CHECK(cert->aliases == std::vector<std::string>{"3.50.1", "Cert"});
    CHECK(cert->availability == Availability::Unavailable);
    CHECK_FALSE(cert->installable());
    CHECK(catalog->find("CERT") == nullptr);
}

TEST_CASE("parse_catalog sorts entries by version, then id") {
    const auto catalog = parse_catalog(read_data("catalog.json"));
    REQUIRE(catalog);
    std::vector<std::string> ids;
    for (const auto& item : catalog->entries) ids.push_back(item.id);
    CHECK(ids == std::vector<std::string>{"cert", "6.1.1", "6.10.1", "12.41"});

    const auto same_version = parse_catalog(bytes(document("[" + entry("b", "4.5") + "," + entry("a", "4.5") + "]")));
    REQUIRE(same_version);
    CHECK(same_version->entries[0].id == "a");
    CHECK(same_version->entries[1].id == "b");
}

TEST_CASE("parse_catalog keys boot_inject by runner") {
    const auto catalog = parse_catalog(read_data("catalog.json"));
    REQUIRE(catalog);

    const BuildFlags early = catalog->flags_for(version("6.1.1"));
    CHECK(early.boot_inject.native == BootStrategy::EarlyBirdApc);
    CHECK(early.boot_inject.wine == BootStrategy::AfterResume);
    CHECK_FALSE(early.boot_inject.umu);
    CHECK_FALSE(early.boot_inject.mac_runtime);
    CHECK(early.xmpp == XmppNote{XmppSupport::Supported, XmppTransport::WebSocket});
    CHECK(early.hotfix_delivery == HotfixDelivery::Serve);

    // proxy_import is not a strategy this build knows, so that runner keeps its default.
    const BuildFlags late = catalog->flags_for(version("12.41"));
    CHECK(late.boot_inject.native == BootStrategy::EarlyBirdApc);
    CHECK_FALSE(late.boot_inject.wine);
    CHECK(late.boot_inject.umu == BootStrategy::AfterResume);
    CHECK_FALSE(late.boot_inject.mac_runtime);
    CHECK(late.auth_exchangecode);
    CHECK(late.openssl_ia32cap);
}

TEST_CASE("flags_for falls back to the defaults outside every range") {
    const auto catalog = parse_catalog(read_data("catalog.json"));
    REQUIRE(catalog);
    CHECK(catalog->flags_for(version("10.31")).hotfix_delivery == HotfixDelivery::Withhold);
    CHECK(catalog->flags_for(version("31.0")) == BuildFlags{});
    CHECK(catalog->flags_for(version("1.7")) == BuildFlags{});
}

TEST_CASE("installable_entries keeps catalog order and skips what cannot be installed") {
    const auto catalog = parse_catalog(read_data("catalog.json"));
    REQUIRE(catalog);
    std::vector<std::string> ids;
    for (const CatalogEntry* item : catalog->installable_entries()) ids.push_back(item->id);
    CHECK(ids == std::vector<std::string>{"6.1.1", "6.10.1", "12.41"});
}

TEST_CASE("parse_catalog maps unknown enum strings to values that are never installed") {
    const auto catalog = parse_catalog(bytes(document(
        R"([{"id":"x","version":"5.10","url":"u","format":"zstd","container":"tar","archive_size":1,"availability":"soon"}])",
        R"([{"first":"5.0","last":"5.40","flags":{"hotfix_delivery":"later","xmpp":{"support":"maybe","transport":"carrier_pigeon"}}}])")));
    REQUIRE(catalog);
    const CatalogEntry& only = catalog->entries.front();
    CHECK(only.format == ArchiveFormat::Unrecognized);
    CHECK(only.container == ArchiveContainer::Unrecognized);
    CHECK(only.availability == Availability::Unavailable);
    CHECK_FALSE(only.installable());
    const BuildFlags flags = catalog->flags_for(only.version);
    CHECK(flags.hotfix_delivery == HotfixDelivery::Withhold);
    CHECK(flags.xmpp == XmppNote{});
}

TEST_CASE("parse_catalog refuses another schema") {
    const CatalogError error = parse_error(R"({"schema":2,"serial":1})");
    CHECK(error.code == CatalogErrorCode::UnknownSchema);
    CHECK(error.schema == 2);
    CHECK(to_diagnostic(error).id == "catalog.unknown_schema");
}

TEST_CASE("parse_catalog names the member that is malformed") {
    CHECK(parse_error("not json").where == "$");
    CHECK(parse_error(document("[" + entry("x", "5.x") + "]")).where == "entries[0].version");
    CHECK(parse_error(document("[" + entry("x", "5.1", R"(,"sha256":"abc")") + "]")).where == "entries[0].sha256");
    CHECK(parse_error(document(R"([{"id":"x"}])")).where == "entries[0].version");

    const CatalogError error = parse_error(document("[" + entry("", "5.1") + "]"));
    CHECK(error.code == CatalogErrorCode::Malformed);
    CHECK(error.where == "entries[0].id");
    CHECK(to_diagnostic(error).id == "catalog.malformed");
}

TEST_CASE("parse_catalog refuses duplicate ids regardless of case") {
    const CatalogError error = parse_error(document("[" + entry("Cert", "3.5") + "," + entry("cert", "3.6") + "]"));
    CHECK(error.code == CatalogErrorCode::Malformed);
    CHECK(error.where == "entries[1].id");
}

TEST_CASE("parse_catalog refuses inverted and overlapping flag ranges") {
    const std::string entries = "[" + entry("x", "5.1") + "]";
    CHECK(parse_error(document(entries, R"([{"first":"5.2","last":"5.1"}])")).where == "flag_ranges[0].last");
    CHECK(parse_error(document(entries, R"([{"first":"5.0","last":"6.0"},{"first":"6.0","last":"7.0"}])")).where ==
          "flag_ranges[1]");
    CHECK(parse_catalog(bytes(document(entries, R"([{"first":"5.0","last":"5.40"},{"first":"6.0","last":"7.0"}])"))));
}

TEST_CASE("parse_catalog refuses an entry no flag range covers") {
    const CatalogError error = parse_error(
        document("[" + entry("x", "5.1") + "," + entry("y", "8.0") + "]", R"([{"first":"5.0","last":"6.0"}])"));
    CHECK(error.code == CatalogErrorCode::Malformed);
    CHECK(error.where == "entries[1].version");
}

TEST_CASE("to_diagnostic gives each error its id and kind") {
    CHECK(to_diagnostic(CatalogError{.code = CatalogErrorCode::EntryNotFound, .entry = "9.99"}).kind == ErrorKind::NotFound);
    CHECK(to_diagnostic(CatalogError{.code = CatalogErrorCode::EntryNotInstallable, .entry = "cert"}).id ==
          "catalog.entry_not_installable");
    const Diagnostic busy = to_diagnostic(
        CatalogError{.code = CatalogErrorCode::HttpStatus, .url = "https://builds.example/catalog.json", .http_status = 503});
    CHECK(busy.id == "catalog.http_status");
    CHECK(busy.retryable);
    CHECK_FALSE(to_diagnostic(CatalogError{.code = CatalogErrorCode::HttpStatus, .http_status = 404}).retryable);
}
