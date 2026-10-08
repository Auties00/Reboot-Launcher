#include <catch2/catch_test_macros.hpp>

#include "reboot/publish/host_status.hpp"

using namespace reboot::publish;

TEST_CASE("a reachable entry is Live whatever its past failures", "[publish]") {
    CHECK(host_status(true, 0) == HostStatus::Live);
    CHECK(host_status(true, 2) == HostStatus::Live);
}

TEST_CASE("an unreachable entry awaits its first probe until one fails", "[publish]") {
    CHECK(host_status(false, 0) == HostStatus::AwaitingProbe);
    CHECK(host_status(false, 1) == HostStatus::LiveUnreachable);
    CHECK(host_status(false, 3) == HostStatus::LiveUnreachable);
}
