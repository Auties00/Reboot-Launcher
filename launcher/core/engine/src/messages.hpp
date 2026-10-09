#pragma once

#include "reboot/foundation/diag.hpp"

namespace rb::engine::msg {

REBOOT_MESSAGE_DECL(kBadCommandLine);
REBOOT_MESSAGE_DECL(kLockFailed);
REBOOT_MESSAGE_DECL(kNotReplaceable);
REBOOT_MESSAGE_DECL(kBusy);
REBOOT_MESSAGE_DECL(kShuttingDown);
REBOOT_MESSAGE_DECL(kAlreadyDraining);
REBOOT_MESSAGE_DECL(kExitPending);
REBOOT_MESSAGE_DECL(kAnswerMismatch);
REBOOT_MESSAGE_DECL(kConflictingPlayTarget);
REBOOT_MESSAGE_DECL(kNotReady);
REBOOT_MESSAGE_DECL(kUnknownView);
REBOOT_MESSAGE_DECL(kUnknownNotice);
REBOOT_MESSAGE_DECL(kInvalidRequest);
REBOOT_MESSAGE_DECL(kImportNeedsShippingChoice);
REBOOT_MESSAGE_DECL(kInstallNotRegistered);
REBOOT_MESSAGE_DECL(kEndpointFailed);
REBOOT_MESSAGE_DECL(kWriteFailed);
REBOOT_MESSAGE_DECL(kCancelled);
REBOOT_MESSAGE_DECL(kNoWineRunner);
REBOOT_MESSAGE_DECL(kSelfTestFailed);

}  // namespace rb::engine::msg
