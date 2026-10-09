#pragma once

#include "reboot/foundation/diag.hpp"

namespace rb::backend::msg {

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
REBOOT_MESSAGE_DECL(kCancelled);
REBOOT_MESSAGE_DECL(kStoppedBeforeReady);
REBOOT_MESSAGE_DECL(kAnswerInvalid);
REBOOT_MESSAGE_DECL(kPathNotUtf8);

}  // namespace rb::backend::msg

namespace rb::backend {

[[nodiscard]] inline DiagBuilder invalid_input(MessageId message) {
    return make_diag(ErrorDomain::Backend, message).kind(ErrorKind::InvalidInput);
}

}  // namespace rb::backend
