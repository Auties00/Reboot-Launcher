#pragma once

#include <string>
#include <string_view>

#include "reboot/foundation/native_path.hpp"

namespace reboot::os_linux::platform {

// file:// URI of an absolute path, with every byte but unreserved ones and '/' percent-encoded.
[[nodiscard]] std::string file_uri(const NativePath& path);

// "https://" (any letter case) followed by a host.
[[nodiscard]] bool is_https_url(std::string_view url) noexcept;

}  // namespace reboot::os_linux::platform
