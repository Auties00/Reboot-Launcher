#pragma once

#include "reboot/foundation/diag.hpp"

namespace reboot::backend::msg {

REBOOT_MESSAGE_DECL(kInvalidUrl);
REBOOT_MESSAGE_DECL(kInvalidPort);
REBOOT_MESSAGE_DECL(kShuttingDown);
REBOOT_MESSAGE_DECL(kInUse);
REBOOT_MESSAGE_DECL(kEmbeddedOnly);
REBOOT_MESSAGE_DECL(kLeaseReleased);
REBOOT_MESSAGE_DECL(kNotReady);
REBOOT_MESSAGE_DECL(kCrashed);
REBOOT_MESSAGE_DECL(kRestartLimitReached);
REBOOT_MESSAGE_DECL(kUnreachable);
REBOOT_MESSAGE_DECL(kUnencryptedUpstreamDeclined);
REBOOT_MESSAGE_DECL(kAccountsNotLoaded);
REBOOT_MESSAGE_DECL(kRemoteLoginFailed);
REBOOT_MESSAGE_DECL(kExchangeCodeFailed);
REBOOT_MESSAGE_DECL(kReconfiguring);
REBOOT_MESSAGE_DECL(kRemoteAddressMissing);

}  // namespace reboot::backend::msg

namespace reboot::backend {

[[nodiscard]] inline DiagBuilder invalid_input(MessageId message) {
    return make_diag(ErrorDomain::Backend, message).kind(ErrorKind::InvalidInput);
}

}  // namespace reboot::backend
