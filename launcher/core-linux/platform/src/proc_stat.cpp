#include "proc_stat.hpp"

#include <charconv>
#include <cstddef>
#include <system_error>

namespace rb::os_linux::platform {

namespace {

// Field 22 (starttime) is the 20th after the closing parenthesis of comm.
constexpr std::size_t kStartTimeIndex = 19;

template <class T>
[[nodiscard]] bool parse_number(std::string_view text, T& out) noexcept {
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), out);
    return error == std::errc{} && end == text.data() + text.size();
}

}  // namespace

std::optional<ProcStat> parse_proc_stat(std::string_view text) {
    const std::size_t open = text.find('(');
    const std::size_t close = text.rfind(')');
    if (open == std::string_view::npos || close == std::string_view::npos || close < open) return std::nullopt;
    ProcStat stat;
    stat.comm = std::string(text.substr(open + 1, close - open - 1));

    std::string_view rest = text.substr(close + 1);
    std::size_t index = 0;
    bool have_ppid = false;
    bool have_start = false;
    while (!rest.empty() && !have_start) {
        const std::size_t begin = rest.find_first_not_of(" \n");
        if (begin == std::string_view::npos) break;
        rest.remove_prefix(begin);
        const std::size_t end = rest.find_first_of(" \n");
        const std::string_view field = rest.substr(0, end);
        rest.remove_prefix(end == std::string_view::npos ? rest.size() : end);
        if (index == 0) {
            if (field.size() != 1) return std::nullopt;
            stat.state = field.front();
        } else if (index == 1) {
            if (!parse_number(field, stat.ppid)) return std::nullopt;
            have_ppid = true;
        } else if (index == kStartTimeIndex) {
            if (!parse_number(field, stat.start_ticks)) return std::nullopt;
            have_start = true;
        }
        ++index;
    }
    if (!have_ppid || !have_start) return std::nullopt;
    return stat;
}

std::optional<u64> parse_boot_time(std::string_view proc_stat) {
    constexpr std::string_view kPrefix = "btime ";
    while (!proc_stat.empty()) {
        const std::size_t end = proc_stat.find('\n');
        const std::string_view line = proc_stat.substr(0, end);
        proc_stat.remove_prefix(end == std::string_view::npos ? proc_stat.size() : end + 1);
        if (!line.starts_with(kPrefix)) continue;
        u64 seconds = 0;
        if (parse_number(line.substr(kPrefix.size()), seconds)) return seconds;
        return std::nullopt;
    }
    return std::nullopt;
}

bool is_steam_reaper(std::string_view comm, std::string_view cmdline) noexcept {
    if (comm != "reaper") return false;
    while (!cmdline.empty()) {
        const std::size_t end = cmdline.find('\0');
        if (cmdline.substr(0, end) == "SteamLaunch") return true;
        if (end == std::string_view::npos) break;
        cmdline.remove_prefix(end + 1);
    }
    return false;
}

}  // namespace rb::os_linux::platform
