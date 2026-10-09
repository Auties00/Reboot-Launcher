#pragma once

#include "reboot/foundation/diag.hpp"
#include "reboot/ports/platform_services.hpp"

namespace rb::ports {

// Windows composition for reboot_client: NamedPipeConnector, WindowsEngineStarter,
// WindowsCallerContext and WindowsClientPaths. One PipeTrust::for_current_process() and one
// WindowsCallerContext::detect() feed them all, so the caller context is captured once.
[[nodiscard]] Result<ClientPlatform> make_client_platform();

}  // namespace rb::ports
