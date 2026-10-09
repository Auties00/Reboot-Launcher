#pragma once

#include <string_view>

namespace rb::integration {

// Only an absolute https URL with a host and no whitespace or control characters.
[[nodiscard]] bool is_openable_url(std::string_view url) noexcept;

}  // namespace rb::integration
