#pragma once

#include "reboot/foundation/diag.hpp"

namespace reboot::os_windows::platform {

REBOOT_MESSAGE_DECL(kLockBusy);
REBOOT_MESSAGE_DECL(kFileVanished);
REBOOT_MESSAGE_DECL(kPayloadHashMismatch);
REBOOT_MESSAGE_DECL(kSecretTooLarge);
REBOOT_MESSAGE_DECL(kWmiTimeout);
REBOOT_MESSAGE_DECL(kWindowsTooOld);
REBOOT_MESSAGE_DECL(kUpdateNotSupported);
REBOOT_MESSAGE_DECL(kVelopackStageFailed);
REBOOT_MESSAGE_DECL(kVelopackApplyFailed);

// Shared by every OS package, so the foundation declares them once.
inline constexpr MessageId kCallFailed{"platform.call_failed"};
inline constexpr MessageId kCallFailedOnPath{"platform.call_failed_on_path"};
inline constexpr MessageId kNotSupported{"platform.not_supported"};
inline constexpr MessageId kNoRemediation{"platform.no_remediation"};
inline constexpr MessageId kUrlNotHttps{"platform.url_not_https"};
inline constexpr MessageId kSecretUnreadable{"platform.secret_unreadable"};

}  // namespace reboot::os_windows::platform
