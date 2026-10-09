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
#include "registry/validation.hpp"

using namespace reboot;
using namespace reboot::browser;

namespace {

struct GoldenBucket {
    std::string version;
    u32 bucket = 0;
};

std::vector<GoldenBucket> read_golden() {
    std::ifstream stream(std::string(REBOOT_BROWSER_TEST_DATA) + "/catalog_version_buckets.txt");
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

}  // namespace

// Every version our catalog names lands in the bucket the edge computes for that string.
TEST_CASE("every catalog version maps to its golden bucket", "[browser]") {
    const auto golden = read_golden();
    REQUIRE(golden.size() == 116);
    for (const auto& row : golden) {
        INFO(row.version);
        const GameVersion parsed = version(row.version);
        CHECK(sb::registry::version_bucket(row.version) == row.bucket);
        CHECK(parsed.bucket() == row.bucket);
        CHECK(buckets_for(parsed) == std::vector<u32>{row.bucket});
    }
}

TEST_CASE("versions match exactly", "[browser]") {
    CHECK(same_game_version("8.51", version("8.51")));
    CHECK(same_game_version("5.01", version("5.1")));
    CHECK_FALSE(same_game_version("8.50", version("8.51")));
    CHECK_FALSE(same_game_version("8.51.1", version("8.51")));
    CHECK_FALSE(same_game_version("5.0.1", version("5.01")));
    CHECK_FALSE(same_game_version("6.0.2", version("6.02")));
    CHECK_FALSE(same_game_version("Cert", version("8.51")));
    CHECK_FALSE(same_game_version("12.41-CL-12905909", version("12.41")));
}
