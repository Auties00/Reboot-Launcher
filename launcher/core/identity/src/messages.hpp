#pragma once

#include "reboot/foundation/diag.hpp"

namespace reboot::identity::msg {

REBOOT_MESSAGE_DECL(kDisplayNameTooShort);
REBOOT_MESSAGE_DECL(kDisplayNameTooLong);
REBOOT_MESSAGE_DECL(kDisplayNameInvalidCharacter);
REBOOT_MESSAGE_DECL(kBackendLoginsNotList);
REBOOT_MESSAGE_DECL(kBackendLoginWithoutEndpoint);
REBOOT_MESSAGE_DECL(kDuplicateBackendLogin);
REBOOT_MESSAGE_DECL(kEmptyRemoteLogin);
REBOOT_MESSAGE_DECL(kLegacyArgvNeedsCustomAuthDll);
REBOOT_MESSAGE_DECL(kLegacyArgvNeedsHostedBackend);
REBOOT_MESSAGE_DECL(kLegacyArgvNeedsLogin);
REBOOT_MESSAGE_DECL(kPasswordInArgv);

}  // namespace reboot::identity::msg
