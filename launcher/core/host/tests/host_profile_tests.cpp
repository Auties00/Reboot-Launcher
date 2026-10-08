#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <string>

#include "messages.hpp"
#include "reboot/host/host_profile.hpp"
#include "reboot/publish/field_limits.hpp"

using namespace reboot;
using namespace reboot::host;

namespace {

constexpr HostProfileId kUserProfileId{
    Uuid{{0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x47, 0x88, 0x99, 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff, 0x00}}};

HostProfile customised(HostProfile profile) {
    profile.listing = HostListing::Listed;
    profile.server_name = "Arena";
    profile.description = "Weekly cup";
    profile.match_end = MatchEndPolicy{MatchEndAction::Shutdown, std::chrono::seconds{30}};
    profile.port = PinnedPorts{Port{7800}};
    return profile;
}

}  // namespace

TEST_CASE("the auto profile is unlisted and maps no ports", "[host]") {
    const HostProfile profile = auto_profile();
    CHECK(profile.is_auto());
    CHECK(profile.is_builtin());
    CHECK(profile.listing == HostListing::Unlisted);
    CHECK_FALSE(profile.port_mapping);
    CHECK(validate(profile));
}

TEST_CASE("a new profile takes the listing setting", "[host]") {
    const HostProfile profile = new_profile(kUserProfileId, "cup", HostListing::Listed);
    CHECK(profile.listing == HostListing::Listed);
    CHECK(std::holds_alternative<AutoPorts>(profile.port));
    CHECK_FALSE(profile.update_policy);
}

TEST_CASE("the auto profile can never be stored Listed", "[host]") {
    HostProfile profile = auto_profile();
    profile.listing = HostListing::Listed;
    const auto validated = validate(profile);
    REQUIRE_FALSE(validated);
    CHECK(validated.error().is(msg::kAutoProfileListed));
}

TEST_CASE("a profile name is required and bounded", "[host]") {
    const auto empty = validate(new_profile(kUserProfileId, "", HostListing::Unlisted));
    REQUIRE_FALSE(empty);
    CHECK(empty.error().is(msg::kProfileNameEmpty));

    const auto long_name =
        validate(new_profile(kUserProfileId, std::string(kMaxProfileNameLength + 1, 'n'), HostListing::Unlisted));
    REQUIRE_FALSE(long_name);
    CHECK(long_name.error().is(msg::kProfileNameTooLong));
}

TEST_CASE("a profile hosts a build or a version without one, not both", "[host]") {
    HostProfile versioned = new_profile(kUserProfileId, "vps", HostListing::Unlisted);
    versioned.version = HostVersion{GameVersion{.major = 12, .minor = 41}, Changelist{12345678}};
    CHECK(validate(versioned));

    HostProfile both = versioned;
    both.build = BuildId{Uuid{{0x01}}};
    const auto validated = validate(both);
    REQUIRE_FALSE(validated);
    CHECK(validated.error().is(msg::kBuildAndVersion));
}

TEST_CASE("published text stays within the rbsb/1 limits", "[host]") {
    HostProfile named = new_profile(kUserProfileId, "cup", HostListing::Unlisted);
    named.server_name = std::string(publish::kMaxServerNameBytes + 1, 's');
    const auto server_name = validate(named);
    REQUIRE_FALSE(server_name);
    CHECK(server_name.error().is(msg::kServerNameTooLong));

    HostProfile described = new_profile(kUserProfileId, "cup", HostListing::Unlisted);
    described.description = std::string(publish::kMaxDescriptionBytes + 1, 'd');
    const auto description = validate(described);
    REQUIRE_FALSE(description);
    CHECK(description.error().is(msg::kDescriptionTooLong));
}

TEST_CASE("the match-end delay is bounded", "[host]") {
    HostProfile profile = new_profile(kUserProfileId, "cup", HostListing::Unlisted);
    profile.match_end.delay = kMaxMatchEndDelay;
    CHECK(validate(profile));
    profile.match_end.delay = kMaxMatchEndDelay + std::chrono::seconds{1};
    const auto validated = validate(profile);
    REQUIRE_FALSE(validated);
    CHECK(validated.error().is(msg::kInvalidMatchEndDelay));
}

TEST_CASE("validate returns the operator policy normalized", "[host]") {
    HostProfile profile = new_profile(kUserProfileId, "cup", HostListing::Unlisted);
    auto address = IpAddress::parse("203.0.113.7");
    REQUIRE(address);
    profile.operators.operator_cidrs = {IpCidr{*address, 24}};
    const auto validated = validate(profile);
    REQUIRE(validated);
    REQUIRE(validated->operators.operator_cidrs.size() == 1);
    CHECK(validated->operators.operator_cidrs[0].to_string() == "203.0.113.0/24");
}

TEST_CASE("a Host reset brings a built-in profile back to its defaults", "[host]") {
    HostProfile profile = customised(new_profile(kDefaultProfileId, "default", HostListing::Unlisted));
    profile.revision = 4;
    const HostProfile reset = reset_profile(profile);
    CHECK(reset.listing == HostListing::Unlisted);
    CHECK(reset.server_name.empty());
    CHECK(reset.description.empty());
    CHECK(reset.match_end == MatchEndPolicy{});
    CHECK(reset.id == kDefaultProfileId);
    CHECK(reset.revision == 4);
    CHECK(reset.port == PortPolicy{PinnedPorts{Port{7800}}});
}

TEST_CASE("a Host reset only unlists a user profile", "[host]") {
    const HostProfile reset = reset_profile(customised(new_profile(kUserProfileId, "cup", HostListing::Unlisted)));
    CHECK(reset.listing == HostListing::Unlisted);
    CHECK(reset.server_name == "Arena");
    CHECK(reset.description == "Weekly cup");
    CHECK(reset.match_end.action == MatchEndAction::Shutdown);
}
