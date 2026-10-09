#pragma once

#include "reboot/foundation/diag.hpp"

namespace rb::os_macos::platform {

REBOOT_MESSAGE_DECL(kCallFailed);
REBOOT_MESSAGE_DECL(kCallFailedOnPath);
REBOOT_MESSAGE_DECL(kNotSupported);
REBOOT_MESSAGE_DECL(kKeychainLocked);
REBOOT_MESSAGE_DECL(kNoGuiSession);
REBOOT_MESSAGE_DECL(kUrlNotHttps);
REBOOT_MESSAGE_DECL(kNotInBundle);
REBOOT_MESSAGE_DECL(kAppTranslocated);
REBOOT_MESSAGE_DECL(kIntegrationForeign);
REBOOT_MESSAGE_DECL(kNoRemediation);
REBOOT_MESSAGE_DECL(kNeedsAppleSilicon);
REBOOT_MESSAGE_DECL(kMacosTooOld);
REBOOT_MESSAGE_DECL(kRosettaMissing);
REBOOT_MESSAGE_DECL(kMetal3Missing);
REBOOT_MESSAGE_DECL(kRosettaInstallFailed);
REBOOT_MESSAGE_DECL(kFirewallBlocksGameServer);
REBOOT_MESSAGE_DECL(kLocalNetworkDenied);
REBOOT_MESSAGE_DECL(kUpdateOutsideBundle);
REBOOT_MESSAGE_DECL(kVelopackStageFailed);
REBOOT_MESSAGE_DECL(kVelopackApplyFailed);
REBOOT_MESSAGE_DECL(kUpdateSwapTimeout);
REBOOT_MESSAGE_DECL(kUpdateNotApplied);
REBOOT_MESSAGE_DECL(kNoHome);
REBOOT_MESSAGE_DECL(kTrashUnavailable);
REBOOT_MESSAGE_DECL(kDnsFailed);
REBOOT_MESSAGE_DECL(kDnsCancelled);
REBOOT_MESSAGE_DECL(kHelperFailed);
REBOOT_MESSAGE_DECL(kHelperTimeout);
REBOOT_MESSAGE_DECL(kNoGateway);

}  // namespace rb::os_macos::platform
