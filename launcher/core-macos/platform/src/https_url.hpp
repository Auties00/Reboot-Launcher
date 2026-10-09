#pragma once

#include <string_view>

namespace reboot::os_macos::platform {

// "https://" in any letter case followed by a host, with no whitespace or control characters.
[[nodiscard]] bool is_https_url(std::string_view url) noexcept;

}  // namespace reboot::os_macos::platform
