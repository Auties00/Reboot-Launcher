#pragma once

#include "reboot/foundation/diag.hpp"

namespace rb::compat::msg {

REBOOT_MESSAGE_DECL(kRunnerUnsupported);
REBOOT_MESSAGE_DECL(kNoRuntime);
REBOOT_MESSAGE_DECL(kRuntimeSetupRequired);
REBOOT_MESSAGE_DECL(kRuntimeSetupRunning);
REBOOT_MESSAGE_DECL(kRuntimeInUse);
REBOOT_MESSAGE_DECL(kRuntimeSetupFailed);
REBOOT_MESSAGE_DECL(kRosettaMissing);
REBOOT_MESSAGE_DECL(kRosettaDeclined);
REBOOT_MESSAGE_DECL(kNoPrefix);
REBOOT_MESSAGE_DECL(kPrefixBusy);
REBOOT_MESSAGE_DECL(kPrefixFailed);
REBOOT_MESSAGE_DECL(kPrefixBackupFailed);
REBOOT_MESSAGE_DECL(kVcRuntimeFailed);
REBOOT_MESSAGE_DECL(kPeMalformed);
REBOOT_MESSAGE_DECL(kDosdevicesUnreadable);
REBOOT_MESSAGE_DECL(kPathNotMapped);
REBOOT_MESSAGE_DECL(kPathNotUtf8);
REBOOT_MESSAGE_DECL(kSessionNotStaged);
REBOOT_MESSAGE_DECL(kSessionAlreadyStaged);
REBOOT_MESSAGE_DECL(kRunnerSpawnFailed);
REBOOT_MESSAGE_DECL(kRunnerExited);
REBOOT_MESSAGE_DECL(kWinhostFatal);
REBOOT_MESSAGE_DECL(kPathNotExposable);
REBOOT_MESSAGE_DECL(kNoVcRedist);
REBOOT_MESSAGE_DECL(kCancelled);
REBOOT_MESSAGE_DECL(kAnswerInvalid);
REBOOT_MESSAGE_DECL(kRecordsNotList);
REBOOT_MESSAGE_DECL(kRecordInvalid);
REBOOT_MESSAGE_DECL(kRecordDuplicate);

}  // namespace rb::compat::msg
