#pragma once

#include <cstring>
#include <span>
#include <string_view>

#include "core/types.hpp"

namespace sb {

[[nodiscard]] constexpr u64 mix64(u64 x) noexcept {
    x ^= x >> 33;
    x *= 0xff51afd7ed558ccdULL;
    x ^= x >> 33;
    x *= 0xc4ceb9fe1a85ec53ULL;
    x ^= x >> 33;
    return x;
}

namespace detail {
[[nodiscard]] inline u64 load64(const u8* p) noexcept {
    u64 v;
    std::memcpy(&v, p, 8);
    return v;
}
[[nodiscard]] inline u64 mum(u64 a, u64 b) noexcept {
    __extension__ using u128 = unsigned __int128;
    const auto r = static_cast<u128>(a) * b;
    return static_cast<u64>(r) ^ static_cast<u64>(r >> 64);
}
}  // namespace detail

// wyhash-style byte hash: not cryptographic, only for in-memory tables.
[[nodiscard]] inline u64 hash_bytes(const void* data, std::size_t len, u64 seed = 0x9E3779B97F4A7C15ULL) noexcept {
    const auto* p = static_cast<const u8*>(data);
    constexpr u64 k0 = 0xa0761d6478bd642fULL, k1 = 0xe7037ed1a0b428dbULL, k2 = 0x8ebc6af09c88c6e3ULL;
    u64 h = seed ^ k0;
    std::size_t i = len;
    while (i >= 16) {
        h = detail::mum(detail::load64(p) ^ k1, detail::load64(p + 8) ^ h);
        p += 16;
        i -= 16;
    }
    u64 a = 0, b = 0;
    if (i >= 8) {
        a = detail::load64(p);
        b = detail::load64(p + i - 8);
    } else if (i > 0) {
        for (std::size_t j = 0; j < i; ++j) a = (a << 8) | p[j];
        b = a >> 3;
    }
    return detail::mum(detail::mum(a ^ k1, b ^ h) ^ k2, len ^ k1);
}

struct UuidHash {
    [[nodiscard]] std::size_t operator()(const Uuid& u) const noexcept {
        return detail::load64(u.bytes.data()) ^ mix64(detail::load64(u.bytes.data() + 8));
    }
};

struct IpHash {
    [[nodiscard]] std::size_t operator()(const IpAddr& a) const noexcept { return hash_bytes(a.bytes.data(), 16); }
};

}  // namespace sb
