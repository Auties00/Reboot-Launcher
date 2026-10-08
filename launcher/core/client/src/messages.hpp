#pragma once

#include "reboot/foundation/diag.hpp"

// Only the client.* ids; the engine-link failures are the ipc.* ids of reboot/ipc/ipc_errors.hpp.
namespace reboot::client::msg {

REBOOT_MESSAGE_DECL(kInvalidArgument);
REBOOT_MESSAGE_DECL(kAbiMismatch);
REBOOT_MESSAGE_DECL(kCallTimedOut);
REBOOT_MESSAGE_DECL(kClosed);

}  // namespace reboot::client::msg
