#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <string_view>
#include <vector>

#include "reboot/updates/pending_update_marker.hpp"

using namespace rb;
using namespace rb::updates;

namespace {

PendingUpdateMarker marker(u32 attempts) {
    return {.from = SemVer{11, 0, 0, ""},
            .to = SemVer{11, 1, 0, ""},
            .attempts = attempts,
            .started_at = std::chrono::system_clock::time_point{std::chrono::seconds{1'760'000'000}}};
}

std::vector<u8> bytes(std::string_view text) { return {text.begin(), text.end()}; }

}  // namespace

TEST_CASE("a marker survives encoding", "[updates]") {
    const PendingUpdateMarker original = marker(1);
    const Result<PendingUpdateMarker> parsed = parse_marker(encode_marker(original));
    REQUIRE(parsed);
    CHECK(*parsed == original);
}

TEST_CASE("a malformed marker names the field", "[updates]") {
    CHECK_FALSE(parse_marker(bytes("not json")));
    CHECK_FALSE(parse_marker(bytes("[]")));

    const Result<PendingUpdateMarker> missing = parse_marker(bytes(R"({"from":"11.0.0","to":"11.1.0"})"));
    REQUIRE_FALSE(missing);
    CHECK(missing.error().id == "updates.marker_malformed");
    const Arg* field = missing.error().find_arg("field");
    REQUIRE(field != nullptr);
    CHECK(std::get<std::string>(*field) == "attempts");

    CHECK_FALSE(parse_marker(bytes(R"({"from":"x","to":"11.1.0","attempts":1,"started_at":0})")));
    CHECK_FALSE(parse_marker(bytes(R"({"from":"11.0.0","to":"11.1.0","attempts":-1,"started_at":0})")));
}

TEST_CASE("the running version decides the verdict", "[updates]") {
    CHECK(judge_marker(marker(0), SemVer{11, 1, 0, ""}) == MarkerVerdict::SelfTest);
    CHECK(judge_marker(marker(kMaxUpdateAttempts), SemVer{11, 1, 0, ""}) == MarkerVerdict::SelfTest);
    CHECK(judge_marker(marker(kMaxUpdateAttempts + 1), SemVer{11, 1, 0, ""}) == MarkerVerdict::GiveUp);
    CHECK(judge_marker(marker(1), SemVer{11, 0, 0, ""}) == MarkerVerdict::NotApplied);
    CHECK(judge_marker(marker(1), SemVer{12, 0, 0, ""}) == MarkerVerdict::Foreign);
}
