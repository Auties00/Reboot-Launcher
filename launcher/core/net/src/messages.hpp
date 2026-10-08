#pragma once

#include "reboot/foundation/diag.hpp"

namespace reboot::net {

REBOOT_MESSAGE_DECL(kInvalidUrl);
REBOOT_MESSAGE_DECL(kDnsFailed);
REBOOT_MESSAGE_DECL(kConnectFailed);
REBOOT_MESSAGE_DECL(kConnectTimeout);
REBOOT_MESSAGE_DECL(kTlsFailed);
REBOOT_MESSAGE_DECL(kRequestTimeout);
REBOOT_MESSAGE_DECL(kTransferStalled);
REBOOT_MESSAGE_DECL(kResponseTooLarge);
REBOOT_MESSAGE_DECL(kPlainHttpNeedsConsent);
REBOOT_MESSAGE_DECL(kHttpsDowngradeRefused);
REBOOT_MESSAGE_DECL(kTransportFailed);
REBOOT_MESSAGE_DECL(kRequestUnbounded);

REBOOT_MESSAGE_DECL(kInsufficientSpace);
REBOOT_MESSAGE_DECL(kRangeNotHonored);
REBOOT_MESSAGE_DECL(kDownloadSourceChanged);
REBOOT_MESSAGE_DECL(kDownloadSizeMismatch);
REBOOT_MESSAGE_DECL(kDownloadHttpStatus);
REBOOT_MESSAGE_DECL(kDownloadWriteFailed);
REBOOT_MESSAGE_DECL(kDownloadAttemptsExhausted);

REBOOT_MESSAGE_DECL(kAddressInvalid);
REBOOT_MESSAGE_DECL(kHostNotFound);
REBOOT_MESSAGE_DECL(kNoIpv4Address);
REBOOT_MESSAGE_DECL(kResolveTimeout);
REBOOT_MESSAGE_DECL(kResolveFailed);

REBOOT_MESSAGE_DECL(kProbePolicyInvalid);

REBOOT_MESSAGE_DECL(kPortBusy);
REBOOT_MESSAGE_DECL(kPortHeldBySystem);
REBOOT_MESSAGE_DECL(kPortAccessDenied);
REBOOT_MESSAGE_DECL(kPortTestFailed);
REBOOT_MESSAGE_DECL(kPortOwnerUnknown);

REBOOT_MESSAGE_DECL(kNoGateway);
REBOOT_MESSAGE_DECL(kMappingRefused);
REBOOT_MESSAGE_DECL(kMappingRenewFailed);
REBOOT_MESSAGE_DECL(kMappingBlockInvalid);

REBOOT_MESSAGE_DECL(kQuicUnavailable);
REBOOT_MESSAGE_DECL(kQuicConnectFailed);
REBOOT_MESSAGE_DECL(kQuicNoIpv4);
REBOOT_MESSAGE_DECL(kQuicUdpBlocked);

}  // namespace reboot::net
