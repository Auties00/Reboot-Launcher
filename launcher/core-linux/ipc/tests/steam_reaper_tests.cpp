#include <catch2/catch_test_macros.hpp>

#include <string_view>

#include "steam_reaper.hpp"

using namespace std::string_view_literals;
using reboot::os_linux::ipc::is_steam_reaper;

TEST_CASE("Steam's reaper launching a game", "[steam_reaper]") {
    CHECK(is_steam_reaper("reaper", "/home/ada/.steam/ubuntu12_32/reaper\0SteamLaunch\0AppId=0\0--\0/usr/bin/x\0"sv));
    CHECK(is_steam_reaper("reaper", "reaper\0SteamLaunch"sv));
}

TEST_CASE("another reaper, or SteamLaunch only as part of an argument", "[steam_reaper]") {
    CHECK_FALSE(is_steam_reaper("reaper", "reaper\0--SteamLaunch\0"sv));
    CHECK_FALSE(is_steam_reaper("reaper", "reaper\0SteamLaunchX\0"sv));
    CHECK_FALSE(is_steam_reaper("reaper", "reaper\0"sv));
    CHECK_FALSE(is_steam_reaper("reaper", ""sv));
}

TEST_CASE("SteamLaunch under any other process name", "[steam_reaper]") {
    CHECK_FALSE(is_steam_reaper("steam", "steam\0SteamLaunch\0"sv));
    CHECK_FALSE(is_steam_reaper("reaper2", "reaper2\0SteamLaunch\0"sv));
    CHECK_FALSE(is_steam_reaper("", "SteamLaunch"sv));
}
