#include <catch2/catch_test_macros.hpp>

#include "firewall_output.hpp"
#include "macos_version.hpp"

using namespace reboot::os_macos::platform;

TEST_CASE("the macOS major version is read from kern.osproductversion", "[macos_version]") {
    CHECK(macos_major_version("14.5") == 14u);
    CHECK(macos_major_version("15.0.1") == 15u);
    CHECK(macos_major_version("26") == 26u);
    CHECK_FALSE(macos_major_version(""));
    CHECK_FALSE(macos_major_version(".5"));
    CHECK_FALSE(macos_major_version("x14.1"));
}

TEST_CASE("socketfilterfw's global state is parsed", "[firewall_output]") {
    CHECK(firewall_enabled("Firewall is enabled. (State = 1)\n") == true);
    CHECK(firewall_enabled("Firewall is disabled. (State = 0)\n") == false);
    CHECK(firewall_enabled("Firewall is blocking all non-essential incoming connections. (State = 2)") == true);
    CHECK_FALSE(firewall_enabled("garbage"));
}

TEST_CASE("socketfilterfw's block-all state is parsed in both wordings", "[firewall_output]") {
    CHECK(firewall_blocks_all("Block all ENABLED!") == true);
    CHECK(firewall_blocks_all("Block all DISABLED!") == false);
    CHECK(firewall_blocks_all("Firewall has block all state set to enabled.") == true);
    CHECK(firewall_blocks_all("Firewall has block all state set to disabled.") == false);
    CHECK_FALSE(firewall_blocks_all(""));
}

TEST_CASE("only an app the firewall blocks counts as blocked", "[firewall_output]") {
    CHECK(firewall_blocks_app("The application /A.app/Contents/MacOS/reboot-game-server is blocked from receiving "
                              "incoming connections."));
    CHECK_FALSE(firewall_blocks_app("The application /A/reboot-game-server is permitted to receive incoming connections."));
    CHECK_FALSE(firewall_blocks_app("The application /A/reboot-game-server is not part of the firewall"));
    CHECK_FALSE(firewall_blocks_app("The application is not blocked"));
}
