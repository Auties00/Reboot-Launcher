#include <catch2/catch_test_macros.hpp>

#include "wait_status.hpp"
#include "watchdog_script.hpp"

using reboot::os_macos::platform::watchdog_script;

TEST_CASE("a group watchdog kills its own process group", "[watchdog_script]") {
    CHECK(watchdog_script(std::nullopt) == "trap '' HUP INT QUIT TERM; read _; kill -KILL 0");
}

TEST_CASE("a pid watchdog kills only that pid", "[watchdog_script]") {
    CHECK(watchdog_script(4242) == "trap '' HUP INT QUIT TERM; read _; kill -KILL 4242");
}

TEST_CASE("wait statuses decode into an exit code or a signal", "[wait_status]") {
    using reboot::os_macos::platform::decode_wait_status;
    const auto exited = decode_wait_status(7 << 8);
    CHECK(exited.code == 7);
    CHECK_FALSE(exited.signal);
    const auto killed = decode_wait_status(9);
    CHECK_FALSE(killed.code);
    CHECK(killed.signal == 9);
    const auto dumped = decode_wait_status(0x80 | 11);
    CHECK(dumped.signal == 11);
    const auto stopped = decode_wait_status((19 << 8) | 0x7F);
    CHECK_FALSE(stopped.code);
    CHECK_FALSE(stopped.signal);
}
