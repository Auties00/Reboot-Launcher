#include "base64.hpp"

#include <array>

namespace rb::os_linux::platform {

namespace {

constexpr std::string_view kAlphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

[[nodiscard]] int value_of(char c) noexcept {
    const std::size_t found = kAlphabet.find(c);
    return found == std::string_view::npos ? -1 : static_cast<int>(found);
}

}  // namespace

std::string base64_encode(std::span<const u8> bytes) {
    std::string out;
    out.reserve((bytes.size() + 2) / 3 * 4);
    for (std::size_t i = 0; i < bytes.size(); i += 3) {
        const std::size_t left = bytes.size() - i;
        const u32 group = (u32{bytes[i]} << 16) | (left > 1 ? u32{bytes[i + 1]} << 8 : 0U) | (left > 2 ? u32{bytes[i + 2]} : 0U);
        out += kAlphabet[(group >> 18) & 0x3F];
        out += kAlphabet[(group >> 12) & 0x3F];
        out += left > 1 ? kAlphabet[(group >> 6) & 0x3F] : '=';
        out += left > 2 ? kAlphabet[group & 0x3F] : '=';
    }
    return out;
}

std::optional<std::vector<u8>> base64_decode(std::string_view text) {
    if (text.size() % 4 != 0) return std::nullopt;
    std::vector<u8> out;
    out.reserve(text.size() / 4 * 3);
    for (std::size_t i = 0; i < text.size(); i += 4) {
        const bool last = i + 4 == text.size();
        const std::size_t padding = last ? (text[i + 3] == '=' ? 1U : 0U) + (text[i + 2] == '=' ? 1U : 0U) : 0U;
        if (padding == 1 && text[i + 3] != '=') return std::nullopt;
        std::array<int, 4> values{};
        for (std::size_t j = 0; j < 4 - padding; ++j) {
            values[j] = value_of(text[i + j]);
            if (values[j] < 0) return std::nullopt;
        }
        const u32 group = (static_cast<u32>(values[0]) << 18) | (static_cast<u32>(values[1]) << 12) |
                          (static_cast<u32>(values[2]) << 6) | static_cast<u32>(values[3]);
        // Bits the padding drops must be zero, so every value has one encoding.
        if ((padding == 1 && (group & 0xFF) != 0) || (padding == 2 && (group & 0xFFFF) != 0)) return std::nullopt;
        out.push_back(static_cast<u8>(group >> 16));
        if (padding < 2) out.push_back(static_cast<u8>(group >> 8));
        if (padding < 1) out.push_back(static_cast<u8>(group));
    }
    return out;
}

}  // namespace rb::os_linux::platform
