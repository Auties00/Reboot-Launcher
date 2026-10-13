#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>

#include "reboot/base/types.hpp"

namespace rb {

namespace detail {

inline constexpr char32_t kReplacementChar = 0xFFFD;

// Decodes the code point at `i` and advances past it. An invalid sequence yields nullopt and
// advances past its maximal subpart, so callers replace each subpart with one U+FFFD.
[[nodiscard]] inline std::optional<char32_t> next_code_point(std::string_view text, std::size_t& i) noexcept {
    const auto byte = [&](std::size_t at) { return static_cast<u8>(text[at]); };
    const u8 lead = byte(i++);
    if (lead < 0x80) return lead;

    std::size_t extra = 0;
    char32_t cp = 0;
    u8 low = 0x80;
    u8 high = 0xBF;
    if (lead >= 0xC2 && lead <= 0xDF) {
        extra = 1;
        cp = lead & 0x1Fu;
    } else if (lead >= 0xE0 && lead <= 0xEF) {
        extra = 2;
        cp = lead & 0x0Fu;
        if (lead == 0xE0) low = 0xA0;
        if (lead == 0xED) high = 0x9F;
    } else if (lead >= 0xF0 && lead <= 0xF4) {
        extra = 3;
        cp = lead & 0x07u;
        if (lead == 0xF0) low = 0x90;
        if (lead == 0xF4) high = 0x8F;
    } else {
        return std::nullopt;
    }
    for (std::size_t k = 0; k < extra; ++k) {
        if (i >= text.size()) return std::nullopt;
        const u8 c = byte(i);
        if (c < low || c > high) return std::nullopt;
        low = 0x80;
        high = 0xBF;
        cp = (cp << 6) | (c & 0x3Fu);
        ++i;
    }
    return cp;
}

inline void append_utf8(std::string& out, char32_t cp) {
    if (cp < 0x80) {
        out.push_back(static_cast<char>(cp));
    } else if (cp < 0x800) {
        out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else if (cp < 0x10000) {
        out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else {
        out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }
}

}  // namespace detail

// Both replace invalid sequences with U+FFFD.
[[nodiscard]] inline std::u16string utf8_to_utf16(std::string_view text) {
    std::u16string out;
    out.reserve(text.size());
    for (std::size_t i = 0; i < text.size();) {
        const char32_t cp = detail::next_code_point(text, i).value_or(detail::kReplacementChar);
        if (cp < 0x10000) {
            out.push_back(static_cast<char16_t>(cp));
        } else {
            out.push_back(static_cast<char16_t>(0xD800 + ((cp - 0x10000) >> 10)));
            out.push_back(static_cast<char16_t>(0xDC00 + ((cp - 0x10000) & 0x3FF)));
        }
    }
    return out;
}

[[nodiscard]] inline std::string utf16_to_utf8(std::u16string_view text) {
    std::string out;
    out.reserve(text.size());
    for (std::size_t i = 0; i < text.size(); ++i) {
        const char16_t unit = text[i];
        char32_t cp = unit;
        if (unit >= 0xD800 && unit <= 0xDBFF && i + 1 < text.size() && text[i + 1] >= 0xDC00 && text[i + 1] <= 0xDFFF) {
            cp = 0x10000 + ((static_cast<char32_t>(unit) - 0xD800) << 10) + (static_cast<char32_t>(text[i + 1]) - 0xDC00);
            ++i;
        } else if (unit >= 0xD800 && unit <= 0xDFFF) {
            cp = detail::kReplacementChar;
        }
        detail::append_utf8(out, cp);
    }
    return out;
}

}  // namespace rb
