#include "messages.hpp"

namespace reboot::integration::msg {

REBOOT_MESSAGE(kNoItems, "integration.no_items", "No integration item was named");
REBOOT_MESSAGE(kForeignEntry, "integration.foreign_entry",
               "The {item} entry belongs to {owner}, so the launcher leaves it as it is");
REBOOT_MESSAGE(kUnsupported, "integration.unsupported", "The {item} entry is not available on this system");
REBOOT_MESSAGE(kInspectFailed, "integration.inspect_failed", "The {item} entry cannot be read");
REBOOT_MESSAGE(kWriteFailed, "integration.write_failed", "The {item} entry cannot be written");
REBOOT_MESSAGE(kRemoveFailed, "integration.remove_failed", "The {item} entry cannot be removed");
REBOOT_MESSAGE(kNotApplied, "integration.not_applied",
               "The {item} entry was written but still does not point at this launcher");

REBOOT_MESSAGE(kEngineInOtherSession, "integration.engine_in_other_session",
               "The launcher engine runs in another sign-in session ({engine_session}), not in yours "
               "({caller_session}), so it cannot open windows for you");
REBOOT_MESSAGE(kNoDisplay, "integration.no_display",
               "Nothing can be opened because this terminal has no graphical display");
REBOOT_MESSAGE(kUrlNotHttps, "integration.url_not_https", "Only https links can be opened");
REBOOT_MESSAGE(kPathNotAbsolute, "integration.path_not_absolute", "{path} is not an absolute path");
REBOOT_MESSAGE(kShellFailed, "integration.shell_failed", "{target} cannot be opened");

REBOOT_MESSAGE(kUnknownPrerequisite, "integration.unknown_prerequisite", "{id} is not a known prerequisite");
REBOOT_MESSAGE(kPrerequisiteNotRemediable, "integration.prerequisite_not_remediable",
               "The launcher cannot fix {id} by itself");
REBOOT_MESSAGE(kPrerequisiteNotApplicable, "integration.prerequisite_not_applicable",
               "{id} does not apply to this system");
REBOOT_MESSAGE(kRemediationFailed, "integration.remediation_failed", "The fix for {id} failed");
REBOOT_MESSAGE(kPrerequisiteStillMissing, "integration.prerequisite_still_missing",
               "{id} is still missing after the fix");

REBOOT_MESSAGE(kGuideWindowsMinVersion, "integration.guide_windows_min_version",
               "Windows 10 version 1809 or later is required");
REBOOT_MESSAGE(kGuideMacAppleSilicon, "integration.guide_mac_apple_silicon", "Playing needs a Mac with Apple silicon");
REBOOT_MESSAGE(kGuideMacMinVersion, "integration.guide_mac_min_version", "Playing needs macOS 14 or later");
REBOOT_MESSAGE(kGuideMacRosetta, "integration.guide_mac_rosetta", "Playing needs Rosetta 2, which the launcher can install");
REBOOT_MESSAGE(kGuideMacAppFirewall, "integration.guide_mac_app_firewall",
               "The macOS firewall may block players from joining servers you host");
REBOOT_MESSAGE(kGuideMacLocalNetwork, "integration.guide_mac_local_network",
               "Allow Local Network access for Reboot Launcher to find and host servers on your network");
REBOOT_MESSAGE(kGuideLinuxPython3, "integration.guide_linux_python3", "Playing needs python3, which umu runs on");
REBOOT_MESSAGE(kGuideLinuxVulkan, "integration.guide_linux_vulkan", "Playing needs a Vulkan driver and the Vulkan loader");
REBOOT_MESSAGE(kGuideLinuxLinger, "integration.guide_linux_linger",
               "Servers you host stop when you sign out unless lingering is enabled for your user");

REBOOT_MESSAGE(kPurgeBlocked, "integration.purge_blocked",
               "This data cannot be deleted while {sessions} sessions run, {operations} operations write to it, "
               "or the backend is in use");
REBOOT_MESSAGE(kPurgeUnsafeTarget, "integration.purge_unsafe_target",
               "{path} is not a launcher data folder, so nothing was deleted");
REBOOT_MESSAGE(kPurgeFailed, "integration.purge_failed", "{path} cannot be deleted");

}  // namespace reboot::integration::msg
