#include <catch2/catch_test_macros.hpp>

#include "reboot/injection/legacy_requirements.hpp"

using namespace reboot;
using namespace reboot::injection;

TEST_CASE("native binds 127.0.0.1:3551 and :80", "[injection][legacy]") {
    const LegacyRequirements requirements = legacy_requirements(ports::RunnerKind::Native);
    REQUIRE(requirements.fixed_listeners.size() == 2);
    CHECK(requirements.fixed_listeners[0] == Endpoint{IpAddress::v4(0x7F000001), kLegacyBackendPort});
    CHECK(requirements.fixed_listeners[1] == Endpoint{IpAddress::v4(0x7F000001), kLegacyXmppPort});
    CHECK(requirements.xmpp_available());
}

TEST_CASE("Wine runners bind :3551 only and have no XMPP", "[injection][legacy]") {
    for (const ports::RunnerKind runner : {ports::RunnerKind::Umu, ports::RunnerKind::Wine, ports::RunnerKind::MacRuntime}) {
        const LegacyRequirements requirements = legacy_requirements(runner);
        REQUIRE(requirements.fixed_listeners.size() == 1);
        CHECK(requirements.fixed_listeners[0] == Endpoint{IpAddress::v4(0x7F000001), kLegacyBackendPort});
        CHECK(requirements.fixed_listeners[0].is_loopback());
        CHECK_FALSE(requirements.xmpp_available());
    }
}
