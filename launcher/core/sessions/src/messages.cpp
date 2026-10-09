#include "messages.hpp"

namespace rb::sessions::msg {

REBOOT_MESSAGE(kNotFound, "sessions.not_found", "There is no session with id {session}");
REBOOT_MESSAGE(kEnded, "sessions.ended", "Session {session} has ended");
REBOOT_MESSAGE(kStopping, "sessions.stopping", "Session {session} is stopping");
REBOOT_MESSAGE(kRefusingNew, "sessions.refusing_new", "The engine is shutting down and starts no new sessions");
REBOOT_MESSAGE(kParentNotLive, "sessions.parent_not_live",
               "Session {session} is not a play session that is starting or running");
REBOOT_MESSAGE(kStopOverran, "sessions.stop_overran",
               "Session {session} did not finish stopping in time, so the engine released it");
REBOOT_MESSAGE(kInvalidTransition, "sessions.invalid_transition", "Session {session} cannot move from {from} to {to}");

}  // namespace rb::sessions::msg
