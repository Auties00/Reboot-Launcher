#pragma once

#include "reboot/foundation/diag.hpp"

namespace rb::secrets::msg {

REBOOT_MESSAGE_DECL(kInvalidScope);
REBOOT_MESSAGE_DECL(kEmptyValue);
REBOOT_MESSAGE_DECL(kTooLarge);
REBOOT_MESSAGE_DECL(kRetentionNotAllowed);
REBOOT_MESSAGE_DECL(kRequestNotPending);
REBOOT_MESSAGE_DECL(kNotFound);
REBOOT_MESSAGE_DECL(kRevealForbidden);
REBOOT_MESSAGE_DECL(kNotReady);
REBOOT_MESSAGE_DECL(kStoreUnavailable);
REBOOT_MESSAGE_DECL(kStoreReadFailed);
REBOOT_MESSAGE_DECL(kStoreWriteFailed);
REBOOT_MESSAGE_DECL(kStoreEraseFailed);
REBOOT_MESSAGE_DECL(kStoreTimedOut);
REBOOT_MESSAGE_DECL(kRequestWithdrawn);
REBOOT_MESSAGE_DECL(kAnswerWithoutSecret);

}  // namespace rb::secrets::msg
