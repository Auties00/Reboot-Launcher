#include <algorithm>
#include <catch2/catch_message.hpp>
#include <catch2/catch_test_macros.hpp>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include "reboot/browser/version_match.hpp"
#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/version.hpp"

using namespace reboot;
using namespace reboot::browser;

namespace {

struct GoldenBucket {
    std::string version;
    u32 bucket = 0;
};

std::vector<GoldenBucket> read_golden() {
    std::ifstream stream(std::string(REBOOT_BROWSER_TEST_DATA) + "/legacy_version_buckets.txt");
    REQUIRE(stream);
    std::vector<GoldenBucket> out;
    for (std::string line; std::getline(stream, line);) {
        if (line.empty() || line.front() == '#') continue;
        std::istringstream fields(line);
        GoldenBucket row;
        REQUIRE(fields >> row.version >> row.bucket);
        out.push_back(row);
    }
    return out;
}

GameVersion version(std::string_view text) {
    auto parsed = GameVersion::parse(text);
    REQUIRE(parsed);
    return *parsed;
}

bool contains(const std::vector<u32>& buckets, u32 bucket) {
    return std::ranges::find(buckets, bucket) != buckets.end();
}

}  // namespace

// server-browser-migration §5: every version 10.0.9 knew lands in the bucket the edge computes.
TEST_CASE("every 10.0.9 catalog and CL-map version maps to its golden bucket", "[browser]") {
    const auto golden = read_golden();
    REQUIRE(golden.size() == 127);
    for (const auto& row : golden) {
        INFO(row.version);
        const GameVersion parsed = version(row.version);
        CHECK(parsed.bucket() == row.bucket);
        CHECK(buckets_for(parsed).front() == row.bucket);
    }
}

TEST_CASE("an aliased build also subscribes to its other spelling's bucket", "[browser]") {
    const auto five = buckets_for(version("5.01"));
    CHECK(contains(five, version("5.01").bucket()));
    CHECK(contains(five, version("5.0.1").bucket()));

    const auto six = buckets_for(version("6.0.2"));
    CHECK(contains(six, version("6.02").bucket()));
    CHECK(contains(six, version("6.0.2").bucket()));

    CHECK(buckets_for(version("8.51")).size() == 1);
}

TEST_CASE("versions match exactly or through the two 10.x aliases", "[browser]") {
    CHECK(same_game_version("8.51", version("8.51")));
    CHECK(same_game_version("5.01", version("5.0.1")));
    CHECK(same_game_version("5.0.1", version("5.01")));
    CHECK(same_game_version("6.02", version("6.0.2")));
    CHECK_FALSE(same_game_version("8.50", version("8.51")));
    CHECK_FALSE(same_game_version("5.10", version("5.0.1")));
    CHECK_FALSE(same_game_version("Cert", version("8.51")));
}
