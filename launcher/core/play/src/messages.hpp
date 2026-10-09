#pragma once

#include "reboot/foundation/diag.hpp"

namespace rb::play::msg {

REBOOT_MESSAGE_DECL(kNoBuildSelected);
REBOOT_MESSAGE_DECL(kBuildVersionUnknown);
REBOOT_MESSAGE_DECL(kSessionRunning);
REBOOT_MESSAGE_DECL(kWrongSession);
REBOOT_MESSAGE_DECL(kNoDisplay);
REBOOT_MESSAGE_DECL(kCustomArgsUnbalancedQuote);
REBOOT_MESSAGE_DECL(kCustomArgsInvalid);
REBOOT_MESSAGE_DECL(kCustomArgsReserved);
REBOOT_MESSAGE_DECL(kUntestedDeclined);
REBOOT_MESSAGE_DECL(kAutoServerDeclined);
REBOOT_MESSAGE_DECL(kExitedBeforeLogin);
REBOOT_MESSAGE_DECL(kCrashed);
REBOOT_MESSAGE_DECL(kHookFailed);
REBOOT_MESSAGE_DECL(kFatal);
REBOOT_MESSAGE_DECL(kCorruptBuild);
REBOOT_MESSAGE_DECL(kAuthFailure);
REBOOT_MESSAGE_DECL(kCannotConnect);
REBOOT_MESSAGE_DECL(kFeaturesDegraded);
REBOOT_MESSAGE_DECL(kLinkedServerEnded);
REBOOT_MESSAGE_DECL(kLinkedServerFailed);
REBOOT_MESSAGE_DECL(kLaunchTimedOut);
REBOOT_MESSAGE_DECL(kSessionStopping);
REBOOT_MESSAGE_DECL(kInvalidAnswer);

}  // namespace rb::play::msg
