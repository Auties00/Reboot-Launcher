#include <array>
#include <catch2/catch_test_macros.hpp>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "reboot/builds/cl_table.hpp"
#include "reboot/builds/detect_version.hpp"
#include "reboot/builds/release_marker.hpp"
#include "reboot/catalog/alias_table.hpp"
#include "reboot/foundation/native_path.hpp"
#include "reboot/foundation/version.hpp"
#include "reboot/testing/golden.hpp"

using namespace rb;
using namespace rb::builds;

namespace {

constexpr std::array<ClTableEntry, 3> kClEntries{{
    {3700114, "1.7.2"},
    {3870737, "2.4.2"},
    {4008490, "3.5"},
}};

GameVersion version(std::string_view text) {
    auto parsed = GameVersion::parse(text);
    REQUIRE(parsed);
    return *parsed;
}

std::vector<u8> golden(std::string_view name) {
    auto bytes = testing::read_golden(NativePath(REBOOT_BUILDS_TEST_DATA) / NativePath(name));
    REQUIRE(bytes);
    return *bytes;
}

std::vector<u8> utf16le(std::string_view ascii) {
    std::vector<u8> out;
    for (const char c : ascii) {
        out.push_back(static_cast<u8>(c));
        out.push_back(0);
    }
    return out;
}

struct GoldenForm {
    std::string_view file;
    std::optional<u32> engine_cl;
    std::string_view tail;
    std::string_view version;
    std::optional<u32> cl;
    VersionSource source;
};

// One VS_VERSIONINFO blob per string form the version-detection decision names.
constexpr std::array<GoldenForm, 5> kForms{{
    {"version_cert.bin", 3700114, "Cert", "1.7.2", 3700114, VersionSource::ClTable},
    {"version_next.bin", 3870737, "Next", "2.4.2", 3870737, VersionSource::ClTable},
    {"version_release_3_5.bin", 4008490, "3.5", "3.5", 4008490, VersionSource::PeResource},
    {"version_release_8_51.bin", std::nullopt, "8.51", "8.51", std::nullopt, VersionSource::PeResource},
    {"version_release_34_10_cl.bin", std::nullopt, "34.10-CL-40567068", "34.10", 40567068, VersionSource::PeResource},
}};

std::optional<Changelist> changelist(std::optional<u32> value) {
    if (!value) return std::nullopt;
    return Changelist{*value};
}

}  // namespace

TEST_CASE("find_release_marker reads every string form from its golden version resource") {
    for (const GoldenForm& form : kForms) {
        INFO(form.file);
        const std::optional<ReleaseMarker> marker = find_release_marker(golden(form.file));
        REQUIRE(marker);
        CHECK(marker->engine_cl == changelist(form.engine_cl));
        CHECK(marker->tail == form.tail);
    }
}

TEST_CASE("derive_version settles every golden string form") {
    const ClTable cl_table(kClEntries);
    const DetectionTables tables{.cl_table = cl_table, .aliases = catalog::AliasTable{}};
    for (const GoldenForm& form : kForms) {
        INFO(form.file);
        const std::optional<ReleaseMarker> marker = find_release_marker(golden(form.file));
        REQUIRE(marker);
        const std::optional<DetectedVersion> detected = derive_version(*marker, VersionSource::PeResource, tables);
        REQUIRE(detected);
        CHECK(detected->version == version(form.version));
        CHECK(detected->cl == changelist(form.cl));
        CHECK(detected->source == form.source);
        CHECK(detected->raw == form.tail);
    }
}

TEST_CASE("find_release_marker finds a marker at an odd byte offset") {
    std::vector<u8> bytes{0x00};
    const std::vector<u8> text = utf16le("4.20.0-4008490+++Fortnite+Release-3.5");
    bytes.insert(bytes.end(), text.begin(), text.end());
    bytes.push_back(0);
    bytes.push_back(0);

    const std::optional<ReleaseMarker> marker = find_release_marker(bytes);
    REQUIRE(marker);
    CHECK(marker->engine_cl == Changelist{4008490});
    CHECK(marker->tail == "3.5");
}

TEST_CASE("find_release_marker skips a tail that is empty, not printable or over the cap") {
    CHECK_FALSE(find_release_marker(utf16le("++Fortnite+Release-")));
    CHECK_FALSE(find_release_marker(utf16le("++Fortnite+Release-3.5\x01")));
    CHECK_FALSE(find_release_marker(utf16le("++Fortnite+Release-" + std::string(kReleaseTailCap + 1, '9'))));
    // A later valid match still counts.
    const std::optional<ReleaseMarker> later =
        find_release_marker(utf16le("++Fortnite+Release-\x01++Fortnite+Release-7.40"));
    REQUIRE(later);
    CHECK(later->tail == "7.40");
}

TEST_CASE("find_release_marker keeps no engine changelist from a malformed prefix") {
    const std::optional<ReleaseMarker> marker = find_release_marker(utf16le("4.16-3700114+++Fortnite+Release-Cert"));
    REQUIRE(marker);
    CHECK_FALSE(marker->engine_cl);
}

TEST_CASE("derive_version never guesses") {
    const ClTable cl_table(kClEntries);
    const DetectionTables tables{.cl_table = cl_table, .aliases = catalog::AliasTable{}};

    // A Cert build whose engine changelist is not in the table, or that carries none.
    CHECK_FALSE(derive_version({.engine_cl = Changelist{3700115}, .tail = "Cert"}, VersionSource::PeResource, tables));
    CHECK_FALSE(derive_version({.engine_cl = std::nullopt, .tail = "Next"}, VersionSource::PeResource, tables));
    // Shapes that are not a version.
    CHECK_FALSE(derive_version({.engine_cl = std::nullopt, .tail = "Main"}, VersionSource::RawScan, tables));
    CHECK_FALSE(derive_version({.engine_cl = std::nullopt, .tail = "12.41-CL-"}, VersionSource::RawScan, tables));
}

TEST_CASE("derive_version keeps the source it was given") {
    const ClTable cl_table(kClEntries);
    const DetectionTables tables{.cl_table = cl_table, .aliases = catalog::AliasTable{}};
    const auto detected =
        derive_version({.engine_cl = std::nullopt, .tail = "12.41-CL-12905909"}, VersionSource::RawScan, tables);
    REQUIRE(detected);
    CHECK(detected->version == version("12.41"));
    CHECK(detected->cl == Changelist{12905909});
    CHECK(detected->source == VersionSource::RawScan);
}

TEST_CASE("ClTable resolves exact changelists only") {
    const ClTable cl_table(kClEntries);
    CHECK(cl_table.lookup(Changelist{3870737}) == version("2.4.2"));
    CHECK_FALSE(cl_table.lookup(Changelist{3870738}));
    CHECK_FALSE(cl_table.lookup(Changelist{1}));
    CHECK_FALSE(cl_table.lookup(Changelist{5000000}));
}
