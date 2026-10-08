#pragma once

#include "reboot/foundation/diag.hpp"

namespace reboot::os_windows::ipc {

REBOOT_MESSAGE_DECL(kEndpointUntrusted);
REBOOT_MESSAGE_DECL(kIpcCallFailed);
REBOOT_MESSAGE_DECL(kIpcCallFailedOnPath);
REBOOT_MESSAGE_DECL(kPipeCallFailed);
REBOOT_MESSAGE_DECL(kPipeNameTaken);
REBOOT_MESSAGE_DECL(kEngineNotListening);
REBOOT_MESSAGE_DECL(kPipeConnectTimedOut);
REBOOT_MESSAGE_DECL(kPipeAccessDenied);
REBOOT_MESSAGE_DECL(kPipeOwnerMismatch);
REBOOT_MESSAGE_DECL(kPipeServerOtherUser);
REBOOT_MESSAGE_DECL(kPipeServerUnverifiable);
REBOOT_MESSAGE_DECL(kPipeClientOtherUser);
REBOOT_MESSAGE_DECL(kPipeClientUnidentified);
REBOOT_MESSAGE_DECL(kSpawnLockTimedOut);
REBOOT_MESSAGE_DECL(kTaskSchedulerTimedOut);
REBOOT_MESSAGE_DECL(kEngineSpawnFailed);

}  // namespace reboot::os_windows::ipc
