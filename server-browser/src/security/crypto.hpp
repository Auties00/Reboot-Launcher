#pragma once

#include <array>
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

template <class T>
[[nodiscard]] std::span<const u8> as_bytes(const T& v) noexcept {
    return {reinterpret_cast<const u8*>(&v), sizeof(T)};
}
[[nodiscard]] inline std::span<const u8> as_bytes(std::string_view s) noexcept {
    return {reinterpret_cast<const u8*>(s.data()), s.size()};
}

}  // namespace sb::security
