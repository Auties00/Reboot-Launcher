#include <catch2/catch_test_macros.hpp>

#include "messages.hpp"
#include "reboot/host/host_start_request.hpp"

using namespace reboot;
using namespace reboot::host;

namespace {

constexpr SessionId kPlaySession{
    Uuid{{0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x47, 0x08, 0x89, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f, 0x10}}};

HostProfile default_profile() { return new_profile(kDefaultProfileId, "default", HostListing::Unlisted); }

HostStartRequest linked_request() {
    HostStartRequest request;
    request.profile = kAutoProfileId;
    request.linked_to = kPlaySession;
    return request;
}

}  // namespace

TEST_CASE("the auto profile starts only linked to a play session", "[host]") {
    CHECK(check_start(linked_request(), auto_profile()));

    HostStartRequest unlinked = linked_request();
    unlinked.linked_to.reset();
    const auto checked = check_start(unlinked, auto_profile());
    REQUIRE_FALSE(checked);
    CHECK(checked.error().is(msg::kAutoProfileNeedsLink));
}

TEST_CASE("a linked server must use the auto profile", "[host]") {
    HostStartRequest request = linked_request();
    request.profile = kDefaultProfileId;
    const auto checked = check_start(request, default_profile());
    REQUIRE_FALSE(checked);
    CHECK(checked.error().is(msg::kLinkedNeedsAutoProfile));
}

TEST_CASE("no override can list the linked auto-server", "[host]") {
    HostStartRequest request = linked_request();
    request.overrides.listing = HostListing::Listed;
    const auto checked = check_start(request, auto_profile());
    REQUIRE_FALSE(checked);
    CHECK(checked.error().is(msg::kAutoProfileListed));

    request.overrides.listing = HostListing::Unlisted;
    CHECK(check_start(request, auto_profile()));
}

TEST_CASE("a user profile may be listed for one start", "[host]") {
    HostStartRequest request;
    request.profile = kDefaultProfileId;
    request.overrides.listing = HostListing::Listed;
    CHECK(check_start(request, default_profile()));
}

TEST_CASE("an override port passes the pinned port rules", "[host]") {
    HostStartRequest request;
    request.profile = kDefaultProfileId;
    request.overrides.port = kReservedBackendPort;
    const auto checked = check_start(request, default_profile());
    REQUIRE_FALSE(checked);
    CHECK(checked.error().is(msg::kReservedPort));

    request.overrides.port = Port{7800};
    CHECK(check_start(request, default_profile()));
}
