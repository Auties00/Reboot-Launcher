#include <catch2/catch_test_macros.hpp>

#include "reboot/publish/listing.hpp"

using namespace reboot::publish;

TEST_CASE("an Unlisted server is always hidden", "[publish]") {
    CHECK(hidden_on_edge(Listing::Unlisted, false));
    CHECK(hidden_on_edge(Listing::Unlisted, true));
}

TEST_CASE("a Listed server is hidden only while restarting", "[publish]") {
    CHECK(hidden_on_edge(Listing::Listed, true));
    CHECK_FALSE(hidden_on_edge(Listing::Listed, false));
}

// discoverable-default: hidden is derived per message, so it cannot stay stuck after a restart.
TEST_CASE("a Listed server going from Restarting to Live is listed again", "[publish]") {
    const Listing pinned = Listing::Listed;
    bool restarting = true;
    const bool hidden_while_restarting = hidden_on_edge(pinned, restarting);
    restarting = false;
    const bool hidden_when_live = hidden_on_edge(pinned, restarting);

    CHECK(hidden_while_restarting);
    CHECK_FALSE(hidden_when_live);
}

TEST_CASE("an Unlisted server going from Restarting to Live stays hidden", "[publish]") {
    CHECK(hidden_on_edge(Listing::Unlisted, true));
    CHECK(hidden_on_edge(Listing::Unlisted, false));
}
