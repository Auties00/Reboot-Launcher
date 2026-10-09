#include "win32.hpp"  // first: sets _WIN32_WINNT before any std header pulls in <windows.h>

#include <catch2/catch_test_macros.hpp>

#include <chrono>

#include "remote_injector.hpp"

using namespace reboot::os_windows::win32session;
using namespace std::chrono_literals;

TEST_CASE("wait_millis never returns INFINITE and clamps non-positive to zero") {
    CHECK(wait_millis(0ms) == 0);
    CHECK(wait_millis(-5ms) == 0);
    CHECK(wait_millis(250ms) == 250);
    CHECK(wait_millis(std::chrono::hours{24 * 365 * 100}) == INFINITE - 1);
    CHECK(wait_millis(std::chrono::milliseconds{INFINITE}) == INFINITE - 1);
}
