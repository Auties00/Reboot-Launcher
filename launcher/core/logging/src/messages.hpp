#pragma once

#include "reboot/foundation/diag.hpp"

namespace reboot::logging::msg {

REBOOT_MESSAGE_DECL(kDirectoryFailed);
REBOOT_MESSAGE_DECL(kOpenFailed);
REBOOT_MESSAGE_DECL(kWriteFailed);
REBOOT_MESSAGE_DECL(kExportDestinationInvalid);
REBOOT_MESSAGE_DECL(kExportWriteFailed);
REBOOT_MESSAGE_DECL(kExportReadFailed);
REBOOT_MESSAGE_DECL(kExportCancelled);

}  // namespace reboot::logging::msg
