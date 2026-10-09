#include <catch2/catch_test_macros.hpp>

#include <string_view>

#include "proc_stat.hpp"

using namespace std::string_view_literals;
using namespace reboot::os_linux::platform;

namespace {

constexpr std::string_view kStat =
    "4242 (wine (server)) S 4100 4242 4242 0 -1 4194560 120 0 0 0 3 1 0 0 20 0 1 0 987654 12345678 300 "
    "18446744073709551615 1 1 0 0 0 0 0 4096 0 0 0 0 17 3 0 0 0 0 0\n";

}  // namespace

TEST_CASE("stat fields after a comm holding spaces and parentheses", "[proc_stat]") {
    const auto stat = parse_proc_stat(kStat);
    REQUIRE(stat);
    CHECK(stat->comm == "wine (server)");
    CHECK(stat->state == 'S');
    CHECK(stat->ppid == 4100);
    CHECK(stat->start_ticks == 987654);
}

TEST_CASE("truncated or malformed stat lines are refused", "[proc_stat]") {
    CHECK_FALSE(parse_proc_stat("4242 (x) S 1 2 3"));
    CHECK_FALSE(parse_proc_stat("4242 x S 1"));
    CHECK_FALSE(parse_proc_stat("4242 (x) S notanumber 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 17 18 19 20"));
    CHECK_FALSE(parse_proc_stat(""));
}

TEST_CASE("the boot time comes from the btime line", "[proc_stat]") {
    CHECK(parse_boot_time("cpu  1 2 3\nintr 5\nbtime 1700000000\nprocesses 9\n") == 1700000000U);
    CHECK_FALSE(parse_boot_time("cpu 1 2 3\n"));
    CHECK_FALSE(parse_boot_time("btime soon\n"));
}

TEST_CASE("Steam's reaper is recognised by comm and its SteamLaunch argument", "[proc_stat]") {
    CHECK(is_steam_reaper("reaper", "/home/ada/.steam/ubuntu12_32/reaper\0SteamLaunch\0AppId=0\0--\0/usr/bin/x\0"sv));
    CHECK_FALSE(is_steam_reaper("reaper", "reaper\0--SteamLaunch\0"sv));
    CHECK_FALSE(is_steam_reaper("steam", "steam\0SteamLaunch\0"sv));
    CHECK_FALSE(is_steam_reaper("reaper", ""sv));
}
