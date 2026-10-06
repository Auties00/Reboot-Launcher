#pragma once

#include <array>
#include <compare>
#include <cstdint>
#include <cstring>
#include <optional>
#include <string>
#include <string_view>

namespace sb {

using u8 = std::uint8_t;
using u16 = std::uint16_t;
using u32 = std::uint32_t;
using u64 = std::uint64_t;
using i32 = std::int32_t;
using i64 = std::int64_t;

struct Uuid {
    std::array<u8, 16> bytes{};

    constexpr auto operator<=>(const Uuid&) const = default;

    [[nodiscard]] constexpr bool is_nil() const noexcept {
        for (auto b : bytes)
            if (b != 0) return false;
        return true;
    }

    [[nodiscard]] std::string to_string() const {
        static constexpr char hex[] = "0123456789abcdef";
        std::string out;
        out.reserve(36);
        for (std::size_t i = 0; i < 16; ++i) {
            if (i == 4 || i == 6 || i == 8 || i == 10) out.push_back('-');
            out.push_back(hex[bytes[i] >> 4]);
            out.push_back(hex[bytes[i] & 0xF]);
        }
        return out;
    }

    // Accepts the canonical 8-4-4-4-12 form or 32 bare hex digits, any case.
    [[nodiscard]] static std::optional<Uuid> parse(std::string_view s) noexcept {
        Uuid u;
        std::size_t n = 0;
        int hi = -1;
        for (char c : s) {
            if (c == '-') continue;
            int v;
            if (c >= '0' && c <= '9') v = c - '0';
            else if (c >= 'a' && c <= 'f') v = c - 'a' + 10;
            else if (c >= 'A' && c <= 'F') v = c - 'A' + 10;
            else return std::nullopt;
            if (hi < 0) {
                hi = v;
            } else {
                if (n >= 16) return std::nullopt;
                u.bytes[n++] = static_cast<u8>((hi << 4) | v);
                hi = -1;
            }
        }
        if (n != 16 || hi >= 0) return std::nullopt;
        return u;
    }
};

// IPv4 is stored as an IPv4-mapped IPv6 address so every address has one representation.
struct IpAddr {
    std::array<u8, 16> bytes{};

    constexpr auto operator<=>(const IpAddr&) const = default;

    [[nodiscard]] static constexpr IpAddr v4(u32 host_order) noexcept {
        IpAddr a;
        a.bytes[10] = 0xFF;
        a.bytes[11] = 0xFF;
        a.bytes[12] = static_cast<u8>(host_order >> 24);
        a.bytes[13] = static_cast<u8>(host_order >> 16);
        a.bytes[14] = static_cast<u8>(host_order >> 8);
        a.bytes[15] = static_cast<u8>(host_order);
        return a;
    }

    [[nodiscard]] constexpr bool is_v4() const noexcept {
        for (int i = 0; i < 10; ++i)
            if (bytes[i] != 0) return false;
        return bytes[10] == 0xFF && bytes[11] == 0xFF;
    }

    // Prefix used for subnet rate limits: /24 for IPv4, /56 for IPv6.
    [[nodiscard]] constexpr IpAddr subnet() const noexcept {
        IpAddr p = *this;
        if (is_v4()) {
            p.bytes[15] = 0;
        } else {
            for (int i = 7; i < 16; ++i) p.bytes[i] = 0;
        }
        return p;
    }

    [[nodiscard]] std::string to_string() const {
        std::string out;
        if (is_v4()) {
            for (int i = 12; i < 16; ++i) {
                if (i > 12) out.push_back('.');
                out += std::to_string(bytes[i]);
            }
            return out;
        }
        static constexpr char hex[] = "0123456789abcdef";
        for (int i = 0; i < 16; i += 2) {
            if (i) out.push_back(':');
            const unsigned v = (unsigned{bytes[i]} << 8) | bytes[i + 1];
            bool started = false;
            for (int shift = 12; shift >= 0; shift -= 4) {
                const unsigned d = (v >> shift) & 0xF;
                if (d || started || shift == 0) {
                    out.push_back(hex[d]);
                    started = true;
                }
            }
        }
        return out;
    }
};

}  // namespace sb
