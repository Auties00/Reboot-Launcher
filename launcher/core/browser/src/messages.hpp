#pragma once

#include "reboot/foundation/diag.hpp"

namespace reboot::browser {

REBOOT_MESSAGE_DECL(kInvalidEndpointOverride);
REBOOT_MESSAGE_DECL(kOffline);
REBOOT_MESSAGE_DECL(kServiceDown);
REBOOT_MESSAGE_DECL(kEdgeUnreachable);
REBOOT_MESSAGE_DECL(kRequestInvalid);
REBOOT_MESSAGE_DECL(kRequestUnsupported);
REBOOT_MESSAGE_DECL(kEdgeInternalError);
REBOOT_MESSAGE_DECL(kEdgeUnavailable);
REBOOT_MESSAGE_DECL(kRateLimited);
REBOOT_MESSAGE_DECL(kNotConnected);
REBOOT_MESSAGE_DECL(kConnectionLost);
REBOOT_MESSAGE_DECL(kRequestTimeout);
REBOOT_MESSAGE_DECL(kRequestCancelled);
REBOOT_MESSAGE_DECL(kInvalidViewSpec);
REBOOT_MESSAGE_DECL(kTooManyViews);
REBOOT_MESSAGE_DECL(kSearchTextLength);
REBOOT_MESSAGE_DECL(kJoinOwnServer);
REBOOT_MESSAGE_DECL(kServerNotFound);
REBOOT_MESSAGE_DECL(kServerOffline);
REBOOT_MESSAGE_DECL(kServerUnreachable);
REBOOT_MESSAGE_DECL(kWrongPassword);
REBOOT_MESSAGE_DECL(kJoinVersionMismatch);
REBOOT_MESSAGE_DECL(kTooManyJoinAttempts);
REBOOT_MESSAGE_DECL(kUnsupportedAddressFamily);
REBOOT_MESSAGE_DECL(kJoinRefused);
REBOOT_MESSAGE_DECL(kInvalidLink);
REBOOT_MESSAGE_DECL(kLinkNotFound);
REBOOT_MESSAGE_DECL(kTargetUnreachable);

}  // namespace reboot::browser
