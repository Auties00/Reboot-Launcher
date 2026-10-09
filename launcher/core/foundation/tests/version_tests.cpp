#include <compare>
#include <iterator>
#include <string>

#include <catch2/catch_test_macros.hpp>

#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/types.hpp"
#include "reboot/foundation/version.hpp"
#include "registry/validation.hpp"

using namespace rb;

namespace {

SemVer semver(std::string_view text) {
    Result<SemVer> parsed = SemVer::parse(text);
    REQUIRE(parsed);
    return *parsed;
}

}  // namespace

TEST_CASE("SemVer parses the 2.0 grammar and drops build metadata", "[foundation][version]") {
    const SemVer v = semver("1.22.333-rc.1+build.5");
    CHECK(v.major == 1);
    CHECK(v.minor == 22);
    CHECK(v.patch == 333);
    CHECK(v.pre == "rc.1");
    CHECK(v.to_string() == "1.22.333-rc.1");
    CHECK(semver("0.0.0").to_string() == "0.0.0");
    CHECK(semver("1.0.0-x-y.0a").pre == "x-y.0a");

    for (const char* bad : {"", "1", "1.2", "1.2.3.4", "01.2.3", "1.02.3", "1.2.3-", "1.2.3-01", "1.2.3-a..b",
                            "1.2.3+", "1.2.3+a_b", "v1.2.3", "1.2.3-\xC3\xA9", "4294967296.0.0", "-1.2.3"}) {
        const Result<SemVer> parsed = SemVer::parse(bad);
        REQUIRE_FALSE(parsed);
        CHECK(parsed.error().id == "foundation.invalid_semver");
        CHECK(parsed.error().kind == ErrorKind::InvalidInput);
    }
}

TEST_CASE("SemVer precedence follows 2.0", "[foundation][version]") {
    const char* ordered[] = {"1.0.0-alpha",      "1.0.0-alpha.1", "1.0.0-alpha.beta", "1.0.0-beta",
                             "1.0.0-beta.2",     "1.0.0-beta.11", "1.0.0-rc.1",       "1.0.0",
                             "1.0.1",            "1.1.0",         "2.0.0"};
    for (std::size_t i = 0; i + 1 < std::size(ordered); ++i) {
        INFO(ordered[i] << " < " << ordered[i + 1]);
        CHECK(semver(ordered[i]) < semver(ordered[i + 1]));
        CHECK(semver(ordered[i + 1]) > semver(ordered[i]));
    }
    CHECK((semver("1.0.0+a") <=> semver("1.0.0+b")) == std::strong_ordering::equal);
}

TEST_CASE("GameVersion parses strictly and canonicalises", "[foundation][version]") {
    const Result<GameVersion> v = GameVersion::parse("12.41");
    REQUIRE(v);
    CHECK(v->major == 12);
    CHECK(v->minor == 41);
    CHECK_FALSE(v->patch);
    CHECK(v->canonical() == "12.41");
    const Result<GameVersion> with_patch = GameVersion::parse("4.5.1");
    REQUIRE(with_patch);
    CHECK(with_patch->canonical() == "4.5.1");
    CHECK(GameVersion::parse("4095.1023.65535")->canonical().size() <= 16);
    CHECK(GameVersion{4, 5, {}} < GameVersion{4, 5, 0});
    CHECK(GameVersion{4, 5, 9} < GameVersion{4, 10, {}});

    for (const char* bad : {"", "12", "12.", ".41", "12.41.1.2", "4096.0", "1.1024", "1.2.65536", "12.41-CL-123",
                            "12.41 ", "+1.2", "a.b"}) {
        const Result<GameVersion> parsed = GameVersion::parse(bad);
        REQUIRE_FALSE(parsed);
        CHECK(parsed.error().id == "foundation.invalid_game_version");
    }
}

TEST_CASE("GameVersion::bucket matches the sb bucket of its canonical form", "[foundation][version]") {
    for (const char* text : {"0.0", "1.8", "12.41", "4095.1023", "7.40.2", "007.040"}) {
        const Result<GameVersion> v = GameVersion::parse(text);
        REQUIRE(v);
        CHECK(v->bucket() == sb::registry::version_bucket(v->canonical()));
    }
    CHECK(GameVersion::parse("12.41")->bucket() == u32{2 + 12 * 1024 + 41});
}

TEST_CASE("parse_uuid takes the canonical form in any case only", "[foundation][types]") {
    const Result<Uuid> lower = parse_uuid("0123abcd-4567-89ef-0123-456789abcdef");
    REQUIRE(lower);
    CHECK(format_uuid(*lower) == "0123abcd-4567-89ef-0123-456789abcdef");
    const Result<Uuid> upper = parse_uuid("0123ABCD-4567-89EF-0123-456789ABCDEF");
    REQUIRE(upper);
    CHECK(*upper == *lower);
    CHECK(hash_uuid(*upper) == hash_uuid(*lower));

    for (const char* bad : {"", "0123abcd456789ef0123456789abcdef", "{0123abcd-4567-89ef-0123-456789abcdef}",
                            "0123abcd-4567-89ef-0123-456789abcdeg", "0123abcd-4567-89ef-0123_456789abcdef",
                            "0123abcd-4567-89ef-0123-456789abcdef0"}) {
        const Result<Uuid> parsed = parse_uuid(bad);
        REQUIRE_FALSE(parsed);
        CHECK(parsed.error().id == "foundation.invalid_uuid");
        CHECK(exit_code_for(parsed.error()) == 2);
    }
}
