#pragma once

#include <optional>
#include <string_view>

#include "reboot/foundation/types.hpp"

namespace reboot::os_macos::platform {

// The major number of kern.osproductversion ("14.5", "15.0.1"); nullopt when it is not one.
[[nodiscard]] std::optional<u32> macos_major_version(std::string_view product_version);

}  // namespace reboot::os_macos::platform
