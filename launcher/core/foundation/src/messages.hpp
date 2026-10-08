#pragma once

#include "reboot/foundation/diag.hpp"

namespace reboot::msg {

REBOOT_MESSAGE_DECL(kInternalBug);
REBOOT_MESSAGE_DECL(kInvalidUuid);
REBOOT_MESSAGE_DECL(kInvalidSemVer);
REBOOT_MESSAGE_DECL(kInvalidGameVersion);
REBOOT_MESSAGE_DECL(kMalformedWirePath);
REBOOT_MESSAGE_DECL(kDataRootNotAbsolute);
REBOOT_MESSAGE_DECL(kNoDataRoot);
REBOOT_MESSAGE_DECL(kOpNotFound);
REBOOT_MESSAGE_DECL(kRequestNotFound);
REBOOT_MESSAGE_DECL(kRequestAlreadyResolved);

}  // namespace reboot::msg
