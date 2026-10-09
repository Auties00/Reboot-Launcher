#pragma once

#include "reboot/foundation/diag.hpp"

namespace rb::integration::msg {

REBOOT_MESSAGE_DECL(kNoItems);
REBOOT_MESSAGE_DECL(kForeignEntry);
REBOOT_MESSAGE_DECL(kUnsupported);
REBOOT_MESSAGE_DECL(kInspectFailed);
REBOOT_MESSAGE_DECL(kWriteFailed);
REBOOT_MESSAGE_DECL(kRemoveFailed);
REBOOT_MESSAGE_DECL(kNotApplied);

REBOOT_MESSAGE_DECL(kEngineInOtherSession);
REBOOT_MESSAGE_DECL(kNoDisplay);
REBOOT_MESSAGE_DECL(kUrlNotHttps);
REBOOT_MESSAGE_DECL(kPathNotAbsolute);
REBOOT_MESSAGE_DECL(kShellFailed);
REBOOT_MESSAGE_DECL(kShellCancelled);

REBOOT_MESSAGE_DECL(kUnknownPrerequisite);
REBOOT_MESSAGE_DECL(kPrerequisiteNotRemediable);
REBOOT_MESSAGE_DECL(kPrerequisiteNotApplicable);
REBOOT_MESSAGE_DECL(kRemediationFailed);
REBOOT_MESSAGE_DECL(kPrerequisiteStillMissing);

REBOOT_MESSAGE_DECL(kGuideWindowsMinVersion);
REBOOT_MESSAGE_DECL(kGuideMacAppleSilicon);
REBOOT_MESSAGE_DECL(kGuideMacMinVersion);
REBOOT_MESSAGE_DECL(kGuideMacRosetta);
REBOOT_MESSAGE_DECL(kGuideMacAppFirewall);
REBOOT_MESSAGE_DECL(kGuideMacLocalNetwork);
REBOOT_MESSAGE_DECL(kGuideLinuxPython3);
REBOOT_MESSAGE_DECL(kGuideLinuxVulkan);
REBOOT_MESSAGE_DECL(kGuideLinuxLinger);

REBOOT_MESSAGE_DECL(kPurgeBlocked);
REBOOT_MESSAGE_DECL(kPurgeUnsafeTarget);
REBOOT_MESSAGE_DECL(kPurgeFailed);

}  // namespace rb::integration::msg
