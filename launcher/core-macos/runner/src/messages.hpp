#pragma once

#include "reboot/foundation/diag.hpp"

namespace reboot::os_macos::runner {

REBOOT_MESSAGE_DECL(kRunnerKindUnsupported);
REBOOT_MESSAGE_DECL(kNeedsAppleSilicon);
REBOOT_MESSAGE_DECL(kWineLoaderMissing);
REBOOT_MESSAGE_DECL(kDxmtMissing);
REBOOT_MESSAGE_DECL(kRuntimeReadFailed);
REBOOT_MESSAGE_DECL(kQuarantineStripFailed);

}  // namespace reboot::os_macos::runner
