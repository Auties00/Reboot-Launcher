#pragma once

#include "reboot/foundation/diag.hpp"

namespace reboot::front::msg {

REBOOT_MESSAGE_DECL(kListenFailed);
REBOOT_MESSAGE_DECL(kNotStarted);
REBOOT_MESSAGE_DECL(kRouteExists);
REBOOT_MESSAGE_DECL(kKeyInUse);
REBOOT_MESSAGE_DECL(kUnknownSession);
REBOOT_MESSAGE_DECL(kUpstreamInvalid);
REBOOT_MESSAGE_DECL(kLegacyFixedInUse);

}  // namespace reboot::front::msg
