#include "messages.hpp"

namespace reboot::os_linux::runner {

REBOOT_MESSAGE(kRunnerKindUnsupported, "platform.linux_runner_kind_unsupported",
               "Linux cannot play with the {runner} runner.");
REBOOT_MESSAGE(kUmuRunMissing, "platform.linux_umu_run_missing", "The umu launcher at {path} has no umu-run.");
REBOOT_MESSAGE(kProtonMissing, "platform.linux_proton_missing", "The GE-Proton runtime at {path} has no proton script.");
REBOOT_MESSAGE(kWineMissing, "platform.linux_wine_missing", "The Wine runtime at {path} lacks {file}.");
REBOOT_MESSAGE(kRuntimeReadFailed, "platform.linux_runtime_read_failed", "{path} could not be read.");
REBOOT_MESSAGE(kPathNotExposable, "platform.linux_path_not_exposable",
               "{path} contains a colon, so the Steam Linux Runtime cannot share it with the game.");
REBOOT_MESSAGE(kScratchPrefixFailed, "platform.linux_scratch_prefix_failed",
               "The temporary prefix {path} for the Steam Linux Runtime setup could not be created.");
REBOOT_MESSAGE(kSlrSetupNotStarted, "platform.linux_slr_setup_not_started",
               "The Steam Linux Runtime setup could not be started.");
REBOOT_MESSAGE(kSlrSetupFailed, "platform.linux_slr_setup_failed",
               "The Steam Linux Runtime setup failed with exit code {exit_code}.");
REBOOT_MESSAGE(kSlrSetupKilled, "platform.linux_slr_setup_killed",
               "The Steam Linux Runtime setup was stopped by signal {signal}.");
REBOOT_MESSAGE(kSlrSetupCancelled, "platform.linux_slr_setup_cancelled", "The Steam Linux Runtime setup was cancelled.");
REBOOT_MESSAGE(kSlrRuntimeUnknown, "platform.linux_slr_runtime_unknown",
               "The GE-Proton runtime at {path} needs a Steam Linux Runtime that umu does not know.");
REBOOT_MESSAGE(kSlrBuildMissing, "platform.linux_slr_build_missing", "{path} does not name a Steam Linux Runtime build.");

}  // namespace reboot::os_linux::runner
