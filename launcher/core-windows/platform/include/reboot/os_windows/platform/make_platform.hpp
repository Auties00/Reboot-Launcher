#pragma once

#include "reboot/foundation/diag.hpp"
#include "reboot/ports/platform_services.hpp"

namespace rb::ports {

// ipc_listener stays empty: core-windows/ipc provides it.
[[nodiscard]] Result<PlatformServices> make_platform(const PlatformOptions& options);

}  // namespace rb::ports
