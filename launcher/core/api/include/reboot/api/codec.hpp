#pragma once

#include <cstddef>
#include <expected>
#include <initializer_list>
#include <optional>
#include <span>
#include <vector>

#include "reboot/api/decode_error.hpp"
#include "reboot/foundation/types.hpp"
#include "wire/codec.hpp"

// reboot.api.v1 messages are generated sb-codec aggregates; these wrap the codec for them.
namespace reboot::api {

using Bytes = std::vector<u8>;

// The members of one oneof, in order; exactly one must be set.
[[nodiscard]] constexpr std::optional<DecodeError> one_case(std::initializer_list<bool> members) noexcept {
    std::size_t set = 0;
    for (const bool member : members)
        if (member) ++set;
    if (set == 0) return DecodeError::UnknownCase;
    if (set > 1) return DecodeError::ConflictingCases;
    return std::nullopt;
}

template <class T>
[[nodiscard]] std::optional<DecodeError> check_all_cases(const std::vector<T>& items) noexcept {
    for (const T& item : items)
        if (auto error = check_cases(item)) return error;
    return std::nullopt;
}

template <sb::wire::Message T>
[[nodiscard]] Bytes encode(const T& message) {
    return sb::wire::encode_to_bytes(message);
}

// Messages holding a oneof also go through their generated check_cases.
template <sb::wire::Message T>
[[nodiscard]] std::expected<T, DecodeError> decode(std::span<const u8> bytes) {
    T message{};
    if (!sb::wire::decode(bytes, message)) return std::unexpected(DecodeError::Malformed);
    if constexpr (requires(const T& m) { check_cases(m); }) {
        if (const std::optional<DecodeError> error = check_cases(message)) return std::unexpected(*error);
    }
    return message;
}

}  // namespace reboot::api
