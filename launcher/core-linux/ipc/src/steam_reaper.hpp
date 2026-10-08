#pragma once

#include <string_view>

namespace reboot::os_linux::ipc {

// Steam's reaper: `comm` is "reaper" and `cmdline`, NUL-separated as /proc/<pid>/cmdline holds
// it, has the argument "SteamLaunch".
[[nodiscard]] bool is_steam_reaper(std::string_view comm, std::string_view cmdline) noexcept;

// Walks the ppid chain from this process up to pid 1 through /proc/<pid>/stat and cmdline: true
// when an ancestor is_steam_reaper. An unreadable entry ends the walk with false.
[[nodiscard]] bool has_steam_reaper_ancestor();

}  // namespace reboot::os_linux::ipc
