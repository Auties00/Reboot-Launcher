#pragma once

#include "reboot/foundation/diag.hpp"
#include "reboot/ports/ipc.hpp"

namespace rb::posix {

// Declared by the IPC port and registered by the foundation.
using ports::kEndpointUntrusted;

}  // namespace rb::posix

namespace rb::os_macos::ipc {

REBOOT_MESSAGE_DECL(kUserTempDirUnavailable);
REBOOT_MESSAGE_DECL(kEndpointOutsideUserTemp);
REBOOT_MESSAGE_DECL(kCallerSessionUnreadable);
REBOOT_MESSAGE_DECL(kAgentRegisterFailed);
REBOOT_MESSAGE_DECL(kAgentRegisterTimedOut);
REBOOT_MESSAGE_DECL(kAgentKickstartFailed);
REBOOT_MESSAGE_DECL(kAgentKickstartTimedOut);
REBOOT_MESSAGE_DECL(kHomeUnavailable);
REBOOT_MESSAGE_DECL(kImageUnresolved);
REBOOT_MESSAGE_DECL(kAppTranslocated);

}  // namespace rb::os_macos::ipc
