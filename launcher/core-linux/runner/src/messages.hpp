#pragma once

#include "reboot/foundation/diag.hpp"

namespace rb::os_linux::runner {

REBOOT_MESSAGE_DECL(kRunnerKindUnsupported);
REBOOT_MESSAGE_DECL(kUmuRunMissing);
REBOOT_MESSAGE_DECL(kProtonMissing);
REBOOT_MESSAGE_DECL(kWineMissing);
REBOOT_MESSAGE_DECL(kRuntimeReadFailed);
REBOOT_MESSAGE_DECL(kPathNotExposable);
REBOOT_MESSAGE_DECL(kScratchPrefixFailed);
REBOOT_MESSAGE_DECL(kSlrSetupNotStarted);
REBOOT_MESSAGE_DECL(kSlrSetupFailed);
REBOOT_MESSAGE_DECL(kSlrSetupKilled);
REBOOT_MESSAGE_DECL(kSlrSetupCancelled);
REBOOT_MESSAGE_DECL(kSlrRuntimeUnknown);
REBOOT_MESSAGE_DECL(kSlrBuildMissing);

}  // namespace rb::os_linux::runner
