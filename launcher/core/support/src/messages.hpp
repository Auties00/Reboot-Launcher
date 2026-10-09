#pragma once

#include "reboot/foundation/diag.hpp"

namespace rb::support::msg {

REBOOT_MESSAGE_DECL(kVersionUnknown);
REBOOT_MESSAGE_DECL(kAboveVersionCap);
REBOOT_MESSAGE_DECL(kAboveVersionCapOptInRequired);
REBOOT_MESSAGE_DECL(kAboveVersionCapOptedIn);
REBOOT_MESSAGE_DECL(kRunnerUnavailable);
REBOOT_MESSAGE_DECL(kHostNeedsNativeRunner);
REBOOT_MESSAGE_DECL(kGameServerUnavailable);
REBOOT_MESSAGE_DECL(kNotCoveredByGameServer);
REBOOT_MESSAGE_DECL(kCustomAuthDll);
REBOOT_MESSAGE_DECL(kExternalBackend);
REBOOT_MESSAGE_DECL(kNoEvidence);
REBOOT_MESSAGE_DECL(kEvidenceStale);
REBOOT_MESSAGE_DECL(kEvidenceFailed);
REBOOT_MESSAGE_DECL(kMalformedServerDescription);
REBOOT_MESSAGE_DECL(kMatrixReportUnknownSchema);
REBOOT_MESSAGE_DECL(kMatrixReportMalformed);

}  // namespace rb::support::msg
