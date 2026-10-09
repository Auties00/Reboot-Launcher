#pragma once

#include <string>
#include <string_view>

namespace reboot::ux {

[[nodiscard]] constexpr bool is_alpha(char c) noexcept { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }
[[nodiscard]] constexpr bool is_digit(char c) noexcept { return c >= '0' && c <= '9'; }
[[nodiscard]] constexpr bool is_alnum(char c) noexcept { return is_alpha(c) || is_digit(c); }
[[nodiscard]] constexpr char to_lower(char c) noexcept { return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c; }
[[nodiscard]] constexpr char to_upper(char c) noexcept { return c >= 'a' && c <= 'z' ? static_cast<char>(c - 'a' + 'A') : c; }

[[nodiscard]] constexpr bool all_alpha(std::string_view s) noexcept {
    for (const char c : s)
        if (!is_alpha(c)) return false;
    return true;
}
[[nodiscard]] constexpr bool all_digit(std::string_view s) noexcept {
    for (const char c : s)
        if (!is_digit(c)) return false;
    return true;
}
[[nodiscard]] constexpr bool all_alnum(std::string_view s) noexcept {
    for (const char c : s)
        if (!is_alnum(c)) return false;
    return true;
}

[[nodiscard]] constexpr bool iequals(std::string_view a, std::string_view b) noexcept {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i)
        if (to_lower(a[i]) != to_lower(b[i])) return false;
    return true;
}

[[nodiscard]] inline std::string to_lower(std::string_view s) {
    std::string out(s);
    for (char& c : out) c = to_lower(c);
    return out;
}
[[nodiscard]] inline std::string to_upper(std::string_view s) {
    std::string out(s);
    for (char& c : out) c = to_upper(c);
    return out;
}
[[nodiscard]] inline std::string to_title(std::string_view s) {
    std::string out = to_lower(s);
    if (!out.empty()) out[0] = to_upper(out[0]);
    return out;
}

}  // namespace reboot::ux
