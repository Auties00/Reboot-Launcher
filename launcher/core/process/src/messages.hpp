#pragma once

#include "reboot/foundation/diag.hpp"

namespace reboot::process::msg {

REBOOT_MESSAGE_DECL(kEnvInvalidName);
REBOOT_MESSAGE_DECL(kEnvInvalidValue);
REBOOT_MESSAGE_DECL(kEnvChannelName);
REBOOT_MESSAGE_DECL(kSpecRelativePath);
REBOOT_MESSAGE_DECL(kSpecEmptyArgument);
REBOOT_MESSAGE_DECL(kSpecInvalidArgument);
REBOOT_MESSAGE_DECL(kSpecNeedsControlChannel);
REBOOT_MESSAGE_DECL(kChildAlreadyStarted);
REBOOT_MESSAGE_DECL(kChildHelloExpected);
REBOOT_MESSAGE_DECL(kChildProtocolMismatch);
REBOOT_MESSAGE_DECL(kChildHelloTimeout);
REBOOT_MESSAGE_DECL(kChildFrameTooLarge);
REBOOT_MESSAGE_DECL(kChildMalformedOutput);
REBOOT_MESSAGE_DECL(kChildUnresponsive);
REBOOT_MESSAGE_DECL(kChildExited);
REBOOT_MESSAGE_DECL(kChildSignaled);
REBOOT_MESSAGE_DECL(kChildRestartLimit);
REBOOT_MESSAGE_DECL(kChildNotRunning);
REBOOT_MESSAGE_DECL(kChildGone);
REBOOT_MESSAGE_DECL(kChildRequestFailed);
REBOOT_MESSAGE_DECL(kChildRequestUnsupported);
REBOOT_MESSAGE_DECL(kChildUnexpectedReply);
REBOOT_MESSAGE_DECL(kReapFailed);
REBOOT_MESSAGE_DECL(kReapCancelled);

}  // namespace reboot::process::msg
