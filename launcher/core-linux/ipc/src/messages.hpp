#pragma once

#include "reboot/foundation/diag.hpp"

namespace reboot::posix {

// Registered once by core/posix, whose public headers do not declare it; this binds to that object.
REBOOT_MESSAGE_DECL(kEndpointUntrusted);

}  // namespace reboot::posix

namespace reboot::os_linux::ipc {

REBOOT_MESSAGE_DECL(kEndpointOutsideRuntimeDir);
REBOOT_MESSAGE_DECL(kInheritedSocketInvalid);
REBOOT_MESSAGE_DECL(kInheritedSocketMismatch);
REBOOT_MESSAGE_DECL(kHomeUnavailable);
REBOOT_MESSAGE_DECL(kImageUnresolved);
REBOOT_MESSAGE_DECL(kEngineSpawnFailed);

}  // namespace reboot::os_linux::ipc
