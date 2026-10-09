#pragma once

#include "reboot/foundation/diag.hpp"

namespace rb::host::msg {

REBOOT_MESSAGE_DECL(kProfileNotFound);
REBOOT_MESSAGE_DECL(kProfileNameEmpty);
REBOOT_MESSAGE_DECL(kProfileNameTooLong);
REBOOT_MESSAGE_DECL(kProfileNameTaken);
REBOOT_MESSAGE_DECL(kServerNameTooLong);
REBOOT_MESSAGE_DECL(kDescriptionTooLong);
REBOOT_MESSAGE_DECL(kProfileStale);
REBOOT_MESSAGE_DECL(kBuiltinProfile);
REBOOT_MESSAGE_DECL(kAutoProfileListed);
REBOOT_MESSAGE_DECL(kInvalidPortPolicy);
REBOOT_MESSAGE_DECL(kReservedPort);
REBOOT_MESSAGE_DECL(kInvalidMatchEndDelay);
REBOOT_MESSAGE_DECL(kInvalidOperatorAddress);
REBOOT_MESSAGE_DECL(kBanWithoutTarget);
REBOOT_MESSAGE_DECL(kProfileBusy);
REBOOT_MESSAGE_DECL(kHostLimitReached);
REBOOT_MESSAGE_DECL(kLinkedNeedsAutoProfile);
REBOOT_MESSAGE_DECL(kAutoProfileNeedsLink);
REBOOT_MESSAGE_DECL(kNoBuildSelected);
REBOOT_MESSAGE_DECL(kBuildVersionUnknown);
REBOOT_MESSAGE_DECL(kBlockOutOfRange);
REBOOT_MESSAGE_DECL(kNoFreeBlock);
REBOOT_MESSAGE_DECL(kListenTimeout);
REBOOT_MESSAGE_DECL(kPortNotOwned);
REBOOT_MESSAGE_DECL(kReadinessTimeout);
REBOOT_MESSAGE_DECL(kNotHostSession);
REBOOT_MESSAGE_DECL(kServerNotRunning);
REBOOT_MESSAGE_DECL(kNotListening);
REBOOT_MESSAGE_DECL(kBuildAndVersion);
REBOOT_MESSAGE_DECL(kBlockInUse);
REBOOT_MESSAGE_DECL(kCancelled);
REBOOT_MESSAGE_DECL(kUntestedDeclined);
REBOOT_MESSAGE_DECL(kInvalidAnswer);
REBOOT_MESSAGE_DECL(kListenFailed);
REBOOT_MESSAGE_DECL(kServerExited);
REBOOT_MESSAGE_DECL(kServerFatal);
REBOOT_MESSAGE_DECL(kServerUnresponsive);
REBOOT_MESSAGE_DECL(kProfileMemberInvalid);
REBOOT_MESSAGE_DECL(kDuplicateProfile);
REBOOT_MESSAGE_DECL(kSecondAutoServerUnpublished);

}  // namespace rb::host::msg
