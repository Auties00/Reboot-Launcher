#pragma once

#include "reboot/foundation/diag.hpp"

namespace rb::posix {

// Registered once by core/posix, whose public headers do not declare them; these bind to those objects.
REBOOT_MESSAGE_DECL(kEndpointUntrusted);
REBOOT_MESSAGE_DECL(kNotADirectory);
REBOOT_MESSAGE_DECL(kDirectoryNotPrivate);
REBOOT_MESSAGE_DECL(kEngineNotListening);

}  // namespace rb::posix

namespace rb::os_linux::ipc {

REBOOT_MESSAGE_DECL(kEndpointOutsideRuntimeDir);
REBOOT_MESSAGE_DECL(kInheritedSocketInvalid);
REBOOT_MESSAGE_DECL(kInheritedSocketMismatch);
REBOOT_MESSAGE_DECL(kHomeUnavailable);
REBOOT_MESSAGE_DECL(kImageUnresolved);
REBOOT_MESSAGE_DECL(kEngineSpawnFailed);

}  // namespace rb::os_linux::ipc
