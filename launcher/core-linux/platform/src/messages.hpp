#pragma once

#include "reboot/foundation/diag.hpp"

namespace reboot::os_linux::platform {

REBOOT_MESSAGE_DECL(kCallFailed);
REBOOT_MESSAGE_DECL(kCallFailedOnPath);
REBOOT_MESSAGE_DECL(kNoHome);
REBOOT_MESSAGE_DECL(kWatchLimit);
REBOOT_MESSAGE_DECL(kSecretServiceTimeout);
REBOOT_MESSAGE_DECL(kSecretUnreadable);
REBOOT_MESSAGE_DECL(kUrlNotHttps);
REBOOT_MESSAGE_DECL(kNoOpener);
REBOOT_MESSAGE_DECL(kTrashUnavailable);
REBOOT_MESSAGE_DECL(kIntegrationForeign);
REBOOT_MESSAGE_DECL(kIntegrationNeedsUserInstall);
REBOOT_MESSAGE_DECL(kSystemdUnavailable);
REBOOT_MESSAGE_DECL(kEngineAgentDefaultRootOnly);
REBOOT_MESSAGE_DECL(kHelperFailed);
REBOOT_MESSAGE_DECL(kPython3Missing);
REBOOT_MESSAGE_DECL(kVulkanLoaderMissing);
REBOOT_MESSAGE_DECL(kLingerDisabled);
REBOOT_MESSAGE_DECL(kNoRemediation);
REBOOT_MESSAGE_DECL(kUpdateNotifyOnly);
REBOOT_MESSAGE_DECL(kUpdatePackageInvalid);
REBOOT_MESSAGE_DECL(kUpdateEntryUnsafe);

}  // namespace reboot::os_linux::platform
