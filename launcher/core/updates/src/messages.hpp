#pragma once

#include "reboot/foundation/diag.hpp"

namespace reboot::updates::msg {

REBOOT_MESSAGE_DECL(kNotifyOnly);
REBOOT_MESSAGE_DECL(kNoUpdate);
REBOOT_MESSAGE_DECL(kBusy);
REBOOT_MESSAGE_DECL(kBelowMinSupported);
REBOOT_MESSAGE_DECL(kCheckFailed);
REBOOT_MESSAGE_DECL(kDownloadFailed);
REBOOT_MESSAGE_DECL(kChecksumMismatch);
REBOOT_MESSAGE_DECL(kStageFailed);
REBOOT_MESSAGE_DECL(kApplyFailed);
REBOOT_MESSAGE_DECL(kNotApplied);
REBOOT_MESSAGE_DECL(kSelfTestFailed);
REBOOT_MESSAGE_DECL(kGaveUp);
REBOOT_MESSAGE_DECL(kStopDeclined);
REBOOT_MESSAGE_DECL(kMarkerMalformed);

}  // namespace reboot::updates::msg
