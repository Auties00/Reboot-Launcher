#pragma once

#include <span>
#include <string>
#include <vector>

#include "reboot/foundation/types.hpp"

namespace rb::os_macos::platform {

// One "-----BEGIN CERTIFICATE-----" block per DER certificate, base64 in 64-column lines.
[[nodiscard]] std::string pem_bundle(std::span<const std::vector<u8>> certificates);

}  // namespace rb::os_macos::platform
