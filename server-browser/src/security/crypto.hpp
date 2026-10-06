#pragma once

#include <array>
#include <ranges>
#include <string>
#include <type_traits>
#include <span>
#include <string_view>

#include "core/types.hpp"

namespace sb::security {

using Digest = std::array<u8, 32>;

[[nodiscard]] Digest sha256(std::span<const u8> data);
[[nodiscard]] Digest hmac_sha256(std::span<const u8> key, std::span<const u8> data);
// HMAC over the concatenation of several parts, avoiding a temporary buffer.
[[nodiscard]] Digest hmac_sha256(std::span<const u8> key, std::initializer_list<std::span<const u8>> parts);
void random_bytes(std::span<u8> out);
[[nodiscard]] bool equal_ct(std::span<const u8> a, std::span<const u8> b) noexcept;

// Object representation of plain values (integers, arrays of bytes). Strings and other ranges
// must go through the overloads below, which view their contents instead.
template <class T>
    requires std::is_trivially_copyable_v<T> && (!std::ranges::range<T>)
[[nodiscard]] std::span<const u8> as_bytes(const T& v) noexcept {
    return {reinterpret_cast<const u8*>(&v), sizeof(T)};
}
[[nodiscard]] inline std::span<const u8> as_bytes(std::string_view s) noexcept {
    return {reinterpret_cast<const u8*>(s.data()), s.size()};
}
[[nodiscard]] inline std::span<const u8> as_bytes(const std::string& s) noexcept {
    return as_bytes(std::string_view(s));
}

}  // namespace sb::security
