#pragma once

#include "reboot/foundation/diag.hpp"

namespace rb::front::msg {

REBOOT_MESSAGE_DECL(kListenFailed);
REBOOT_MESSAGE_DECL(kNotStarted);
REBOOT_MESSAGE_DECL(kRouteExists);
REBOOT_MESSAGE_DECL(kKeyInUse);
REBOOT_MESSAGE_DECL(kUnknownSession);
REBOOT_MESSAGE_DECL(kUpstreamInvalid);
REBOOT_MESSAGE_DECL(kLegacyFixedInUse);
REBOOT_MESSAGE_DECL(kLegacyFixedCancelled);
REBOOT_MESSAGE_DECL(kUnexpectedAnswer);

}  // namespace rb::front::msg
