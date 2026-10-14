#include <catch2/catch_message.hpp>
#include <catch2/catch_test_macros.hpp>

#include "reboot/posix/ifaddrs_network_interfaces.hpp"
#include "reboot/testing/port_conformance.hpp"

using namespace rb;

TEST_CASE("IfaddrsNetworkInterfaces passes the network interfaces suite") {
    posix::IfaddrsNetworkInterfaces interfaces;
    const testing::ConformanceReport report = testing::run_network_interfaces_conformance(interfaces);
    INFO(report.describe());
    CHECK(report.passed());
}
