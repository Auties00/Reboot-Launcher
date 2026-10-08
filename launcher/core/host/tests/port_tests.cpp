#include <catch2/catch_test_macros.hpp>
#include <vector>

#include "messages.hpp"
#include "reboot/host/port_block.hpp"
#include "reboot/host/port_policy.hpp"

using namespace reboot;
using namespace reboot::host;

using gameserver::SocketRole;

TEST_CASE("a port block lists its ports in order", "[host]") {
    const PortBlock block{Port{7777}, 2};
    REQUIRE(block.fits());
    CHECK(block.at(1) == Port{7778});
    CHECK(block.last() == Port{7778});
    CHECK(block.ports() == std::vector<Port>{Port{7777}, Port{7778}});
    CHECK(block.contains(Port{7778}));
    CHECK_FALSE(block.contains(Port{7779}));
    CHECK_FALSE(block.contains(Port{7776}));
}

TEST_CASE("an empty block or one past 65535 does not fit", "[host]") {
    CHECK_FALSE(PortBlock{Port{7777}, 0}.fits());
    CHECK_FALSE(PortBlock{Port{65535}, 2}.fits());
    CHECK(PortBlock{Port{65534}, 2}.fits());
}

TEST_CASE("blocks overlap only when they share a port", "[host]") {
    const PortBlock block{Port{7777}, 2};
    CHECK(block.overlaps(PortBlock{Port{7778}, 2}));
    CHECK_FALSE(block.overlaps(PortBlock{Port{7779}, 2}));
    CHECK_FALSE(block.overlaps(PortBlock{Port{7775}, 2}));
}

TEST_CASE("the game port is the first Game socket's", "[host]") {
    const PortBlock block{Port{7777}, 2};
    CHECK(game_port(block, {SocketRole::Game, SocketRole::Beacon}) == Port{7777});
    CHECK(game_port(block, {SocketRole::Beacon, SocketRole::Game}) == Port{7778});
}

TEST_CASE("a pinned port must be a user port other than the backend's", "[host]") {
    CHECK(validate(PortPolicy{PinnedPorts{}}));
    CHECK(validate(PortPolicy{PinnedPorts{kMinHostPort}}));

    const auto low = validate(PortPolicy{PinnedPorts{Port{1023}}});
    REQUIRE_FALSE(low);
    CHECK(low.error().is(msg::kInvalidPortPolicy));

    const auto reserved = validate(PortPolicy{PinnedPorts{kReservedBackendPort}});
    REQUIRE_FALSE(reserved);
    CHECK(reserved.error().is(msg::kReservedPort));
}

TEST_CASE("an auto range may span the backend port, whose blocks are skipped", "[host]") {
    CHECK(validate(PortPolicy{AutoPorts{}}));
    CHECK(validate(PortPolicy{AutoPorts{PortRange{Port{3000}, Port{4000}}}}));
}

TEST_CASE("an auto range must be ordered and above the system ports", "[host]") {
    const auto inverted = validate(PortPolicy{AutoPorts{PortRange{Port{7786}, Port{7777}}}});
    REQUIRE_FALSE(inverted);
    CHECK(inverted.error().is(msg::kInvalidPortPolicy));

    const auto low = validate(PortPolicy{AutoPorts{PortRange{Port{80}, Port{8080}}}});
    REQUIRE_FALSE(low);
    CHECK(low.error().is(msg::kInvalidPortPolicy));
}
