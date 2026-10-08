#pragma once

#include "reboot/foundation/diag.hpp"

namespace reboot::posix {

// Registered once by core/posix, whose public headers do not declare it; this binds to that object.
REBOOT_MESSAGE_DECL(kEndpointUntrusted);

}  // namespace reboot::posix

namespace reboot::os_macos::ipc {

REBOOT_MESSAGE_DECL(kUserTempDirUnavailable);
REBOOT_MESSAGE_DECL(kEndpointOutsideUserTemp);
REBOOT_MESSAGE_DECL(kCallerSessionUnreadable);
REBOOT_MESSAGE_DECL(kAgentRegisterFailed);
REBOOT_MESSAGE_DECL(kAgentRegisterTimedOut);
REBOOT_MESSAGE_DECL(kAgentKickstartFailed);
REBOOT_MESSAGE_DECL(kAgentKickstartTimedOut);

}  // namespace reboot::os_macos::ipc
