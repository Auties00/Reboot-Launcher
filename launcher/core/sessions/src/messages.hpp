#pragma once

#include "reboot/foundation/diag.hpp"

namespace rb::sessions::msg {

REBOOT_MESSAGE_DECL(kNotFound);
REBOOT_MESSAGE_DECL(kEnded);
REBOOT_MESSAGE_DECL(kStopping);
REBOOT_MESSAGE_DECL(kRefusingNew);
REBOOT_MESSAGE_DECL(kParentNotLive);
REBOOT_MESSAGE_DECL(kInvalidTransition);
REBOOT_MESSAGE_DECL(kStopOverran);

}  // namespace rb::sessions::msg
