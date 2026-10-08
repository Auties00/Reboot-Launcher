#pragma once

#include "reboot/foundation/diag.hpp"

namespace reboot::engine::msg {

REBOOT_MESSAGE_DECL(kBadCommandLine);
REBOOT_MESSAGE_DECL(kLockFailed);
REBOOT_MESSAGE_DECL(kNotReplaceable);
REBOOT_MESSAGE_DECL(kBusy);
REBOOT_MESSAGE_DECL(kShuttingDown);
REBOOT_MESSAGE_DECL(kAlreadyDraining);
REBOOT_MESSAGE_DECL(kExitPending);
REBOOT_MESSAGE_DECL(kAnswerMismatch);
REBOOT_MESSAGE_DECL(kConflictingPlayTarget);

}  // namespace reboot::engine::msg
