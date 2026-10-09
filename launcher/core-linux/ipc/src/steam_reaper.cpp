#include "steam_reaper.hpp"

#include <charconv>
#include <cstddef>
#include <fstream>
#include <iterator>
#include <optional>
#include <string>
#include <system_error>

namespace rb::os_linux::ipc {
namespace {

constexpr std::string_view kSteamLaunchArgument = "SteamLaunch";
// A pid reused during the walk cannot loop it forever.
constexpr int kMaxAncestors = 256;

struct ProcStat {
    std::string comm;
    long ppid = 0;
};

[[nodiscard]] std::optional<std::string> read_proc_file(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) return std::nullopt;
    std::string text{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
    if (file.bad()) return std::nullopt;
    return text;
}

// "<pid> (<comm>) <state> <ppid> ..."; comm may hold ')' itself, so the last one closes it.
[[nodiscard]] std::optional<ProcStat> read_proc_stat(const std::string& pid) {
    const std::optional<std::string> stat = read_proc_file("/proc/" + pid + "/stat");
    if (!stat) return std::nullopt;
    const std::size_t open = stat->find('(');
    const std::size_t close = stat->rfind(')');
    if (open == std::string::npos || close == std::string::npos || close < open) return std::nullopt;
    const std::size_t state = stat->find_first_not_of(' ', close + 1);
    if (state == std::string::npos) return std::nullopt;
    const std::size_t ppid_begin = stat->find_first_not_of(' ', state + 1);
    if (ppid_begin == std::string::npos) return std::nullopt;
    ProcStat result{stat->substr(open + 1, close - open - 1), 0};
    const char* const end = stat->data() + stat->size();
    if (std::from_chars(stat->data() + ppid_begin, end, result.ppid).ec != std::errc{}) return std::nullopt;
    return result;
}

}  // namespace

bool is_steam_reaper(std::string_view comm, std::string_view cmdline) noexcept {
    if (comm != "reaper") return false;
    while (!cmdline.empty()) {
        const std::size_t end = cmdline.find('\0');
        if (cmdline.substr(0, end) == kSteamLaunchArgument) return true;
        if (end == std::string_view::npos) break;
        cmdline.remove_prefix(end + 1);
    }
    return false;
}

bool has_steam_reaper_ancestor() {
    const std::optional<ProcStat> self = read_proc_stat("self");
    if (!self) return false;
    long pid = self->ppid;
    for (int depth = 0; pid >= 1 && depth < kMaxAncestors; ++depth) {
        const std::string pid_text = std::to_string(pid);
        const std::optional<ProcStat> stat = read_proc_stat(pid_text);
        const std::optional<std::string> cmdline = read_proc_file("/proc/" + pid_text + "/cmdline");
        if (!stat || !cmdline) return false;
        if (is_steam_reaper(stat->comm, *cmdline)) return true;
        if (pid == 1) return false;
        pid = stat->ppid;
    }
    return false;
}

}  // namespace rb::os_linux::ipc
