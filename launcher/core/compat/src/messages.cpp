#include "messages.hpp"

namespace reboot::compat::msg {

REBOOT_MESSAGE(kRunnerUnsupported, "compat.runner_unsupported", "This system cannot play with the {runner} runner");
REBOOT_MESSAGE(kNoRuntime, "compat.no_runtime", "The release manifest selects no runtime for the {runner} runner");
REBOOT_MESSAGE(kRuntimeSetupRequired, "compat.runtime_setup_required",
               "The {runner} runner needs its runtime setup before the first game");
REBOOT_MESSAGE(kRuntimeSetupRunning, "compat.runtime_setup_running",
               "The runtime setup of the {runner} runner is still running");
REBOOT_MESSAGE(kRuntimeInUse, "compat.runtime_in_use", "The {runner} runner is in use by a running game");
REBOOT_MESSAGE(kRuntimeSetupFailed, "compat.runtime_setup_failed", "The runtime setup of the {runner} runner failed");
REBOOT_MESSAGE(kRosettaMissing, "compat.rosetta_missing", "Rosetta 2 is still not installed");
REBOOT_MESSAGE(kRosettaDeclined, "compat.rosetta_declined", "Playing on this Mac needs Rosetta 2");
REBOOT_MESSAGE(kNoPrefix, "compat.no_prefix", "The {runner} runner has no Wine prefix");
REBOOT_MESSAGE(kPrefixBusy, "compat.prefix_busy",
               "The Wine prefix of the {runner} runner needs maintenance, but session {holder} is using it");
REBOOT_MESSAGE(kPrefixFailed, "compat.prefix_failed",
               "Cannot prepare the Wine prefix of the {runner} runner at step {step}");
REBOOT_MESSAGE(kPrefixBackupFailed, "compat.prefix_backup_failed",
               "Cannot back up the Wine prefix of the {runner} runner before switching to an older runtime");
REBOOT_MESSAGE(kVcRuntimeFailed, "compat.vc_runtime_failed",
               "Cannot install the Visual C++ runtime in the Wine prefix of the {runner} runner");
REBOOT_MESSAGE(kPeMalformed, "compat.pe_malformed", "The import table of {path} is malformed at offset {offset}");
REBOOT_MESSAGE(kDosdevicesUnreadable, "compat.dosdevices_unreadable", "Cannot read the drives of the Wine prefix at {path}");
REBOOT_MESSAGE(kPathNotMapped, "compat.path_not_mapped", "{path} is outside every drive of the Wine prefix");
REBOOT_MESSAGE(kPathNotUtf8, "compat.path_not_utf8", "{path} has a name that Windows programs cannot open");
REBOOT_MESSAGE(kSessionNotStaged, "compat.session_not_staged", "Session {session} has no Wine setup");
REBOOT_MESSAGE(kSessionAlreadyStaged, "compat.session_already_staged", "Session {session} already has a Wine setup");
REBOOT_MESSAGE(kRunnerSpawnFailed, "compat.runner_spawn_failed", "Cannot start the {runner} runner");
REBOOT_MESSAGE(kRunnerExited, "compat.runner_exited", "The {runner} runner exited before the game did");

}  // namespace reboot::compat::msg
