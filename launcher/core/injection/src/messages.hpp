#pragma once

#include "reboot/foundation/diag.hpp"

namespace rb::injection::msg {

REBOOT_MESSAGE_DECL(kDllPathEmpty);
REBOOT_MESSAGE_DECL(kDllMissing);
REBOOT_MESSAGE_DECL(kDllNotDll);
REBOOT_MESSAGE_DECL(kDllNotPe64);
REBOOT_MESSAGE_DECL(kDllUnreadable);
REBOOT_MESSAGE_DECL(kInjectFailed);

}  // namespace rb::injection::msg
