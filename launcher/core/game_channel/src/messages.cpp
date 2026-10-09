#include "messages.hpp"

namespace rb::game_channel::msg {

REBOOT_MESSAGE(kListenFailed, "game_channel.listen_failed", "The game control channel cannot listen on 127.0.0.1");
REBOOT_MESSAGE(kNotListening, "game_channel.not_listening", "The game control channel is not listening");
REBOOT_MESSAGE(kBadPreamble, "game_channel.bad_preamble",
               "A connection to the game control channel did not start with the control preamble");
REBOOT_MESSAGE(kPayloadAbiMismatch, "game_channel.payload_abi_mismatch",
               "The game component uses payload ABI {actual}, but this launcher needs {expected}");
REBOOT_MESSAGE(kProtocolMismatch, "game_channel.protocol_mismatch",
               "The {role} uses control protocol {actual}, but this launcher needs {expected}");
REBOOT_MESSAGE(kHelloTimeout, "game_channel.hello_timeout",
               "A game component connected but did not identify itself within {timeout}");
REBOOT_MESSAGE(kUnknownToken, "game_channel.unknown_token",
               "A game component presented a token this launcher did not issue or has already ended");
REBOOT_MESSAGE(kRoleMismatch, "game_channel.role_mismatch",
               "A game component connected as {actual} with a token issued for {expected}");
REBOOT_MESSAGE(kDuplicatePeer, "game_channel.duplicate_peer", "The {role} {module} is already connected for this session");
REBOOT_MESSAGE(kPeerLost, "game_channel.peer_lost", "The connection to the {role} closed while the game was running");
REBOOT_MESSAGE(kNotWelcomed, "game_channel.not_welcomed", "The {role} has not connected yet");
REBOOT_MESSAGE(kUnsupportedRequest, "game_channel.unsupported_request", "The {role} does not support {request}");
REBOOT_MESSAGE(kRequestFailed, "game_channel.request_failed", "The {role} could not complete {request}");
REBOOT_MESSAGE(kTestModeOff, "game_channel.test_mode_off", "{request} is available only when the game runs in test mode");
REBOOT_MESSAGE(kLogUnreadable, "game_channel.log_unreadable", "The game log {path} cannot be read");

}  // namespace rb::game_channel::msg
