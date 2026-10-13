#pragma once

#include <expected>
#include <optional>

namespace rb::testing {

// libc++ cannot check `std::expected == value` inside CHECK or REQUIRE: the constraint of its
// operator== recurses through Catch's ExprLhs. Tests compare these optionals instead.
template <class T, class E>
[[nodiscard]] std::optional<T> held_value(const std::expected<T, E>& result) {
    if (!result) return std::nullopt;
    return *result;
}

template <class T, class E>
[[nodiscard]] std::optional<E> held_error(const std::expected<T, E>& result) {
    if (result) return std::nullopt;
    return result.error();
}

}  // namespace rb::testing
