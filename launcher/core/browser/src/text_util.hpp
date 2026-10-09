#pragma once

#include <string_view>

namespace rb::browser {

[[nodiscard]] constexpr bool is_ascii_space(char c) noexcept {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v';
}

[[nodiscard]] constexpr std::string_view trim_ascii(std::string_view text) noexcept {
    while (!text.empty() && is_ascii_space(text.front())) text.remove_prefix(1);
    while (!text.empty() && is_ascii_space(text.back())) text.remove_suffix(1);
    return text;
}

}  // namespace rb::browser
