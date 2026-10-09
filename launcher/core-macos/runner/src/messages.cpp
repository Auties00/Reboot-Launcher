#include "messages.hpp"

namespace rb::os_macos::runner {

REBOOT_MESSAGE(kRunnerKindUnsupported, "platform.mac_runner_kind_unsupported",
               "macOS cannot play with the {runner} runner.");
REBOOT_MESSAGE(kNeedsAppleSilicon, "platform.mac_runner_needs_apple_silicon",
               "Playing on macOS needs an Apple Silicon Mac.");
REBOOT_MESSAGE(kWineLoaderMissing, "platform.mac_runtime_wine_missing", "The runtime at {path} has no Wine loader.");
REBOOT_MESSAGE(kDxmtMissing, "platform.mac_runtime_dxmt_missing", "The runtime at {path} lacks the DXMT file {file}.");
REBOOT_MESSAGE(kRuntimeReadFailed, "platform.mac_runtime_read_failed", "{path} could not be read.");
REBOOT_MESSAGE(kQuarantineStripFailed, "platform.mac_quarantine_strip_failed",
               "The quarantine flag could not be removed from {path}.");

}  // namespace rb::os_macos::runner
