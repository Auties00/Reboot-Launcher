#pragma once

#include <array>
#include <cstddef>
#include <optional>
#include <string_view>

#include "reboot/foundation/types.hpp"

namespace rb::trust {

[[nodiscard]] constexpr int hex_digit(char c) noexcept {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

[[nodiscard]] constexpr bool is_hex(std::string_view text) noexcept {
    for (const char c : text)
        if (hex_digit(c) < 0) return false;
    return true;
}

[[nodiscard]] constexpr bool is_lower_hex(std::string_view text) noexcept {
    for (const char c : text)
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return false;
    return true;
}

template <std::size_t N>
[[nodiscard]] constexpr std::optional<std::array<u8, N>> decode_hex(std::string_view text) noexcept {
    if (text.size() != 2 * N || !is_hex(text)) return std::nullopt;
    std::array<u8, N> out{};
    for (std::size_t i = 0; i < N; ++i)
        out[i] = static_cast<u8>(hex_digit(text[2 * i]) * 16 + hex_digit(text[2 * i + 1]));
    return out;
}

}  // namespace rb::trust
