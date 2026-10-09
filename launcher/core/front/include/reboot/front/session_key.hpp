#pragma once

#include <array>
#include <cstddef>
#include <cstring>
#include <functional>
#include <optional>
#include <string>
#include <string_view>

#include "reboot/foundation/types.hpp"

namespace rb {
class IRandom;
}

namespace rb::front {

// 128 CSPRNG bits, 32 lowercase hex digits in /s/<key>/; copyable on purpose to route, while redacted in logs.
struct SessionKey {
    std::array<u8, 16> bytes{};

    [[nodiscard]] static SessionKey generate(IRandom& random);
    // Exactly 32 hex digits, either case.
    [[nodiscard]] static std::optional<SessionKey> parse(std::string_view hex);
    [[nodiscard]] std::string to_hex() const;

    // Constant time, since a request's key is compared against the live ones.
    friend constexpr bool operator==(const SessionKey& a, const SessionKey& b) noexcept {
        u8 diff = 0;
        for (std::size_t i = 0; i < a.bytes.size(); ++i) diff |= static_cast<u8>(a.bytes[i] ^ b.bytes[i]);
        return diff == 0;
    }
};

}  // namespace rb::front

template <>
struct std::hash<rb::front::SessionKey> {
    std::size_t operator()(const rb::front::SessionKey& key) const noexcept {
        rb::u64 lo = 0;
        rb::u64 hi = 0;
        std::memcpy(&lo, key.bytes.data(), sizeof lo);
        std::memcpy(&hi, key.bytes.data() + sizeof lo, sizeof hi);
        return static_cast<std::size_t>(lo ^ (hi * 0x9E3779B97F4A7C15ull));
    }
};
