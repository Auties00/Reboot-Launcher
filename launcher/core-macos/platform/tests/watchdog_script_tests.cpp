#include <catch2/catch_test_macros.hpp>

#include "watchdog_script.hpp"

using reboot::os_macos::platform::watchdog_script;

TEST_CASE("a group watchdog kills its own process group", "[watchdog_script]") {
    CHECK(watchdog_script(std::nullopt) == "read _; kill -KILL 0");
}

TEST_CASE("a pid watchdog kills only that pid", "[watchdog_script]") {
    CHECK(watchdog_script(4242) == "read _; kill -KILL 4242");
}
