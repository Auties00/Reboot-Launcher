#include "reboot/logging/log_file_names.hpp"

#include <charconv>
#include <format>
#include <system_error>

namespace reboot::logging {

namespace {

constexpr std::string_view kSessionPrefix = "launcher-";
constexpr std::string_view kWinePrefix = "wine-";
constexpr std::string_view kProtonPrefix = "steam-";
constexpr std::string_view kLogSuffix = ".log";
constexpr std::size_t kStampLength = 16;  // yyyymmddThhmmssZ
constexpr std::size_t kUuidLength = 36;

[[nodiscard]] bool is_digit(char c) noexcept { return c >= '0' && c <= '9'; }

[[nodiscard]] bool all_digits(std::string_view text) noexcept {
    if (text.empty()) return false;
    for (const char c : text)
        if (!is_digit(c)) return false;
    return true;
}

// Decimal without a leading zero, so each value has one spelling.
template <class T>
[[nodiscard]] std::optional<T> parse_decimal(std::string_view text) noexcept {
    if (!all_digits(text) || (text.size() > 1 && text.front() == '0')) return std::nullopt;
    T value{};
    const auto [end, ec] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (ec != std::errc{} || end != text.data() + text.size()) return std::nullopt;
    return value;
}

[[nodiscard]] int two_digits(std::string_view text, std::size_t at) noexcept {
    return (text[at] - '0') * 10 + (text[at + 1] - '0');
}

[[nodiscard]] std::optional<std::chrono::sys_seconds> parse_stamp(std::string_view text) noexcept {
    if (text.size() != kStampLength || text[8] != 'T' || text[15] != 'Z') return std::nullopt;
    if (!all_digits(text.substr(0, 8)) || !all_digits(text.substr(9, 6))) return std::nullopt;
    const int year = two_digits(text, 0) * 100 + two_digits(text, 2);
    const std::chrono::year_month_day date{std::chrono::year{year},
                                           std::chrono::month{static_cast<unsigned>(two_digits(text, 4))},
                                           std::chrono::day{static_cast<unsigned>(two_digits(text, 6))}};
    const int hours = two_digits(text, 9);
    const int minutes = two_digits(text, 11);
    const int seconds = two_digits(text, 13);
    if (!date.ok() || hours > 23 || minutes > 59 || seconds > 59) return std::nullopt;
    return std::chrono::sys_days{date} + std::chrono::hours{hours} + std::chrono::minutes{minutes} +
           std::chrono::seconds{seconds};
}

[[nodiscard]] bool is_lower_hex(char c) noexcept { return is_digit(c) || (c >= 'a' && c <= 'f'); }

// Uuid::to_string's form: lower-case 8-4-4-4-12.
[[nodiscard]] bool is_uuid_text(std::string_view text) noexcept {
    if (text.size() != kUuidLength) return false;
    for (std::size_t i = 0; i < text.size(); ++i) {
        const bool dash = i == 8 || i == 13 || i == 18 || i == 23;
        if (dash ? text[i] != '-' : !is_lower_hex(text[i])) return false;
    }
    return true;
}

[[nodiscard]] std::string group_prefix(std::string_view kind_prefix, const LogFileGroup& group) {
    return std::format("{}{:%Y%m%dT%H%M%SZ}-{}-", kind_prefix, group.started_at, group.pid);
}

// Splits "<stamp>-<pid>-<rest>" and parses the group.
[[nodiscard]] std::optional<LogFileGroup> parse_group(std::string_view text, std::string_view& rest) noexcept {
    if (text.size() < kStampLength + 1 || text[kStampLength] != '-') return std::nullopt;
    const auto started_at = parse_stamp(text.substr(0, kStampLength));
    if (!started_at) return std::nullopt;
    text.remove_prefix(kStampLength + 1);
    const std::size_t dash = text.find('-');
    if (dash == std::string_view::npos) return std::nullopt;
    const auto pid = parse_decimal<u32>(text.substr(0, dash));
    if (!pid) return std::nullopt;
    rest = text.substr(dash + 1);
    return LogFileGroup{*started_at, *pid};
}

[[nodiscard]] bool is_session_tail(std::string_view tail) noexcept {
    const std::size_t dot = tail.find('.');
    if (dot == std::string_view::npos) return is_valid_log_role(tail);
    const auto part = parse_decimal<u32>(tail.substr(dot + 1));
    return is_valid_log_role(tail.substr(0, dot)) && part && *part > 0;
}

}  // namespace

bool is_valid_log_role(std::string_view role) noexcept {
    if (role.empty() || role.size() > 16) return false;
    for (const char c : role)
        if (!is_digit(c) && (c < 'a' || c > 'z')) return false;
    return true;
}

std::string session_log_file_name(const LogFileGroup& group, std::string_view role, u32 part) {
    std::string name = group_prefix(kSessionPrefix, group);
    name += role;
    if (part > 0) name += std::format(".{}", part);
    name += kLogSuffix;
    return name;
}

std::string wine_log_file_name(const LogFileGroup& group, const SessionId& session) {
    return group_prefix(kWinePrefix, group) + format_uuid(session.value) + std::string(kLogSuffix);
}

std::optional<ParsedLogFileName> classify_log_file(std::string_view file_name) noexcept {
    if (!file_name.ends_with(kLogSuffix)) return std::nullopt;
    file_name.remove_suffix(kLogSuffix.size());

    std::string_view rest;
    if (file_name.starts_with(kSessionPrefix)) {
        const auto group = parse_group(file_name.substr(kSessionPrefix.size()), rest);
        if (!group || !is_session_tail(rest)) return std::nullopt;
        return ParsedLogFileName{LogFileKind::Session, group};
    }
    if (file_name.starts_with(kWinePrefix)) {
        const auto group = parse_group(file_name.substr(kWinePrefix.size()), rest);
        if (!group || !is_uuid_text(rest)) return std::nullopt;
        return ParsedLogFileName{LogFileKind::Wine, group};
    }
    if (file_name.starts_with(kProtonPrefix) && all_digits(file_name.substr(kProtonPrefix.size())))
        return ParsedLogFileName{LogFileKind::ProtonLog, std::nullopt};
    return std::nullopt;
}

}  // namespace reboot::logging
