#pragma once

#include "reboot/foundation/diag.hpp"

namespace rb::os_windows::platform {

// Ids shared with the other OS packages carry the same English everywhere.
REBOOT_MESSAGE_DECL(kCallFailed);
REBOOT_MESSAGE_DECL(kCallFailedOnPath);
REBOOT_MESSAGE_DECL(kNotSupported);
REBOOT_MESSAGE_DECL(kNoRemediation);
REBOOT_MESSAGE_DECL(kUrlNotHttps);
REBOOT_MESSAGE_DECL(kSecretUnreadable);
REBOOT_MESSAGE_DECL(kTrashUnavailable);
REBOOT_MESSAGE_DECL(kLockBusy);
REBOOT_MESSAGE_DECL(kFileVanished);
REBOOT_MESSAGE_DECL(kPayloadHashMismatch);
REBOOT_MESSAGE_DECL(kSecretTooLarge);
REBOOT_MESSAGE_DECL(kSessionStuck);
REBOOT_MESSAGE_DECL(kWmiTimeout);
REBOOT_MESSAGE_DECL(kWindowsTooOld);
REBOOT_MESSAGE_DECL(kUpdateNotSupported);
REBOOT_MESSAGE_DECL(kVelopackStageFailed);
REBOOT_MESSAGE_DECL(kVelopackApplyFailed);

}  // namespace rb::os_windows::platform
