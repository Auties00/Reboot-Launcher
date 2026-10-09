#pragma once

#include <string_view>

namespace rb::os_windows::platform {

// "https://" in any letter case, then a non-empty host, and no whitespace or control characters,
// which ShellExecute would otherwise split or pass to another handler.
[[nodiscard]] bool is_https_url(std::string_view url) noexcept;

}  // namespace rb::os_windows::platform
