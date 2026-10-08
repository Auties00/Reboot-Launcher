#pragma once

#include <chrono>
#include <optional>
#include <string>
#include <string_view>

#include "reboot/foundation/types.hpp"

namespace reboot::logging {

enum class LogFileKind : u8 { Session, Wine, ProtonLog };

// One process run, the unit retention counts as a session.
struct LogFileGroup {
    // Second resolution, so a group parsed back from a file name compares equal.
    std::chrono::sys_seconds started_at;
    u32 pid = 0;

    bool operator==(const LogFileGroup&) const = default;
};

struct ParsedLogFileName {
    LogFileKind kind{};
    // Absent for Proton logs, which Proton names and appends to across runs.
    std::optional<LogFileGroup> group;
};

// 1 to 16 of [a-z0-9]; no '-', so a session file name parses back unambiguously.
[[nodiscard]] bool is_valid_log_role(std::string_view role) noexcept;

// launcher-<yyyymmddThhmmssZ>-<pid>-<role>.log for part 0, then <...>-<role>.<part>.log.
// Amends logging-redaction section 2 (".1", ".2"): the part goes before .log so editors still open it.
[[nodiscard]] std::string session_log_file_name(const LogFileGroup& group, std::string_view role, u32 part);

// wine-<yyyymmddThhmmssZ>-<pid>-<session uuid>.log.
// Amends logging-redaction section 2 (wine-<session>.log): the run's group lets retention count it with that run.
[[nodiscard]] std::string wine_log_file_name(const LogFileGroup& group, const SessionId& session);

// Session, Wine and Proton's steam-<game id>.log; retention and export never touch other files.
[[nodiscard]] std::optional<ParsedLogFileName> classify_log_file(std::string_view file_name) noexcept;

}  // namespace reboot::logging
