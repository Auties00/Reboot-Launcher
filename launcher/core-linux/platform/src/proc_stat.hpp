#pragma once

#include <optional>
#include <string>
#include <string_view>

#include "reboot/foundation/types.hpp"

namespace reboot::os_linux::platform {

// The fields of /proc/<pid>/stat this package reads.
struct ProcStat {
    std::string comm;
    char state = '?';
    u32 ppid = 0;
    // Field 22: clock ticks after boot.
    u64 start_ticks = 0;
};

// "<pid> (<comm>) <state> <ppid> ..."; comm may hold ')' and spaces, so the last ')' closes it.
[[nodiscard]] std::optional<ProcStat> parse_proc_stat(std::string_view text);

// The "btime <seconds>" line of /proc/stat.
[[nodiscard]] std::optional<u64> parse_boot_time(std::string_view proc_stat);

// Steam's reaper: `comm` is "reaper" and `cmdline`, NUL-separated as /proc/<pid>/cmdline holds
// it, has the argument "SteamLaunch".
[[nodiscard]] bool is_steam_reaper(std::string_view comm, std::string_view cmdline) noexcept;

}  // namespace reboot::os_linux::platform
