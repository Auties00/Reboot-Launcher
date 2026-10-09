#pragma once

#include <cstddef>
#include <string>
#include <string_view>

namespace rb {
class IRandom;
}

namespace rb::identity {

// `count` chars drawn uniformly from `alphabet`, which holds at most 256 chars.
[[nodiscard]] std::string random_chars(IRandom& random, std::string_view alphabet, std::size_t count);

[[nodiscard]] constexpr bool is_ascii_alnum(char c) noexcept {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9');
}

// Every byte outside [A-Za-z0-9] removed.
[[nodiscard]] std::string keep_ascii_alnum(std::string_view text);

}  // namespace rb::identity
