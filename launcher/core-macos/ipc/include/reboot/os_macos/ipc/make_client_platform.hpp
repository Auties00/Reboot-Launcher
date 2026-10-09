#pragma once

#include "reboot/foundation/diag.hpp"
#include "reboot/ports/platform_services.hpp"

namespace rb::ports {

// macOS composition for reboot_client: UnixSocketConnector, SmAppServiceEngineStarter,
// MacCallerContext and MacClientPaths. One MacClientPaths::detect() and one
// MacCallerContext::detect() feed them all, so the caller context is captured once.
[[nodiscard]] Result<ClientPlatform> make_client_platform();

}  // namespace rb::ports
