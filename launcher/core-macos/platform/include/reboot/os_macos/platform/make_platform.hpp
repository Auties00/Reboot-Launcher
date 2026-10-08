#pragma once

#include "reboot/foundation/diag.hpp"
#include "reboot/ports/platform_services.hpp"

namespace reboot::ports {

// The macOS adapters; runner comes from core-macos/runner and ipc_listener from core-macos/ipc.
[[nodiscard]] Result<PlatformServices> make_platform(const PlatformOptions& options);

}  // namespace reboot::ports
