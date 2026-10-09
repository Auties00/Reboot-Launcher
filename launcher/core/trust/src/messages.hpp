#pragma once

#include "reboot/foundation/diag.hpp"

namespace rb::trust {

REBOOT_MESSAGE_DECL(kSignatureMalformed);
REBOOT_MESSAGE_DECL(kUnknownKey);
REBOOT_MESSAGE_DECL(kSignatureInvalid);
REBOOT_MESSAGE_DECL(kSerialRollback);
REBOOT_MESSAGE_DECL(kSerialPersistFailed);
REBOOT_MESSAGE_DECL(kCryptoFailure);
REBOOT_MESSAGE_DECL(kDocumentExpired);

}  // namespace rb::trust
