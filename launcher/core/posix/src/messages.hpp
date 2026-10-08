#pragma once

#include "reboot/foundation/diag.hpp"

namespace reboot::posix {

// Registered here for macOS and Linux and by os_windows/ipc on Windows; core/ipc leaves it to them.
REBOOT_MESSAGE_DECL(kEndpointUntrusted);
REBOOT_MESSAGE_DECL(kCallFailed);
REBOOT_MESSAGE_DECL(kCallFailedOnPath);
REBOOT_MESSAGE_DECL(kSocketPathTooLong);
REBOOT_MESSAGE_DECL(kNotADirectory);
// {mode} is an octal string such as "0755", never an integer, which would print in decimal.
REBOOT_MESSAGE_DECL(kDirectoryNotPrivate);
REBOOT_MESSAGE_DECL(kPeerOtherUser);
// ErrorKind::EngineUnavailable and retryable.
REBOOT_MESSAGE_DECL(kEngineNotListening);
// ErrorKind::Conflict.
REBOOT_MESSAGE_DECL(kLockBusy);
REBOOT_MESSAGE_DECL(kHeldFileChanged);

}  // namespace reboot::posix
