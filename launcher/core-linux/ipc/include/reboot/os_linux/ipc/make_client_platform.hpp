#pragma once

#include "reboot/foundation/diag.hpp"
#include "reboot/ports/platform_services.hpp"

namespace rb::ports {

// Linux composition for reboot_client: UnixSocketConnector, SystemdEngineStarter,
// LinuxCallerContext and LinuxClientPaths. One LinuxClientPaths::detect(), one
// LinuxCallerContext::detect() and one Steam reaper ancestry walk feed them all, so the caller
// context is captured once.
[[nodiscard]] Result<ClientPlatform> make_client_platform();

}  // namespace rb::ports
