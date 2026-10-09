#pragma once

#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "reboot/foundation/types.hpp"

namespace reboot::os_linux::platform {

// RFC 4648 with padding.
[[nodiscard]] std::string base64_encode(std::span<const u8> bytes);
// nullopt for anything but canonical padded base64.
[[nodiscard]] std::optional<std::vector<u8>> base64_decode(std::string_view text);

}  // namespace reboot::os_linux::platform
