#pragma once

#include "reboot/foundation/diag.hpp"

namespace rb::game_channel::msg {

REBOOT_MESSAGE_DECL(kListenFailed);
REBOOT_MESSAGE_DECL(kNotListening);
REBOOT_MESSAGE_DECL(kBadPreamble);
REBOOT_MESSAGE_DECL(kPayloadAbiMismatch);
REBOOT_MESSAGE_DECL(kProtocolMismatch);
REBOOT_MESSAGE_DECL(kHelloTimeout);
REBOOT_MESSAGE_DECL(kUnknownToken);
REBOOT_MESSAGE_DECL(kRoleMismatch);
REBOOT_MESSAGE_DECL(kDuplicatePeer);
REBOOT_MESSAGE_DECL(kPeerLost);
REBOOT_MESSAGE_DECL(kNotWelcomed);
REBOOT_MESSAGE_DECL(kUnsupportedRequest);
REBOOT_MESSAGE_DECL(kRequestFailed);
REBOOT_MESSAGE_DECL(kTestModeOff);
REBOOT_MESSAGE_DECL(kLogUnreadable);

}  // namespace rb::game_channel::msg
