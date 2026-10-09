#pragma once

#include "reboot/foundation/diag.hpp"

namespace rb::publish::msg {

REBOOT_MESSAGE_DECL(kNotPublished);
REBOOT_MESSAGE_DECL(kAlreadyPublished);
REBOOT_MESSAGE_DECL(kProfileBusy);
REBOOT_MESSAGE_DECL(kNotRegistered);

REBOOT_MESSAGE_DECL(kServerNameEmpty);
REBOOT_MESSAGE_DECL(kMaxPlayersTooHigh);
REBOOT_MESSAGE_DECL(kPlayerCountTooHigh);
REBOOT_MESSAGE_DECL(kPasswordTooLong);
REBOOT_MESSAGE_DECL(kGamePortMissing);

REBOOT_MESSAGE_DECL(kEdgeUnreachable);
REBOOT_MESSAGE_DECL(kConnectionLost);
REBOOT_MESSAGE_DECL(kEdgeGoAway);
REBOOT_MESSAGE_DECL(kEdgeRateLimited);
REBOOT_MESSAGE_DECL(kEdgeUnavailable);
REBOOT_MESSAGE_DECL(kEdgeRejected);
REBOOT_MESSAGE_DECL(kEdgeHostLimit);
REBOOT_MESSAGE_DECL(kEdgeNotAllowed);

REBOOT_MESSAGE_DECL(kIdentityRotated);
REBOOT_MESSAGE_DECL(kHostedElsewhere);
REBOOT_MESSAGE_DECL(kTokenNotSaved);

REBOOT_MESSAGE_DECL(kIdentityDirUnavailable);
REBOOT_MESSAGE_DECL(kIdentityInUse);
REBOOT_MESSAGE_DECL(kIdentityNotRegistered);
REBOOT_MESSAGE_DECL(kExportDestinationExists);
REBOOT_MESSAGE_DECL(kIdentityFileInvalid);
REBOOT_MESSAGE_DECL(kIdentityUnreadable);

}  // namespace rb::publish::msg
