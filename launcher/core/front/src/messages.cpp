#include "messages.hpp"

namespace rb::front::msg {

REBOOT_MESSAGE(kListenFailed, "front.listen_failed", "Could not open the game's loopback endpoint on {address}");
REBOOT_MESSAGE(kNotStarted, "front.not_started", "The game's loopback endpoint is not running");
REBOOT_MESSAGE(kRouteExists, "front.route_exists", "Session {session} already has a loopback route");
REBOOT_MESSAGE(kKeyInUse, "front.key_in_use", "The session key is already used by another session");
REBOOT_MESSAGE(kUnknownSession, "front.unknown_session", "Session {session} has no loopback route");
REBOOT_MESSAGE(kUpstreamInvalid, "front.upstream_invalid", "{url} is not a valid http, https, ws or wss address");
REBOOT_MESSAGE(kLegacyFixedInUse, "front.legacy_fixed_in_use",
               "Session {session} already uses the fixed ports of the custom authentication DLL");
REBOOT_MESSAGE(kLegacyFixedCancelled, "front.legacy_fixed_cancelled",
               "Opening the fixed ports for session {session} was cancelled");
REBOOT_MESSAGE(kUnexpectedAnswer, "front.unexpected_answer", "The answer does not fit the question about {origin}");

}  // namespace rb::front::msg
