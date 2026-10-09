#include "messages.hpp"

namespace reboot::host::msg {

REBOOT_MESSAGE(kProfileNotFound, "host.profile_not_found", "There is no host profile {profile}");
REBOOT_MESSAGE(kProfileNameEmpty, "host.profile_name_empty", "A host profile needs a name");
REBOOT_MESSAGE(kProfileNameTooLong, "host.profile_name_too_long", "A host profile name can be at most {limit} bytes");
REBOOT_MESSAGE(kProfileNameTaken, "host.profile_name_taken", "A host profile named {name} already exists");
REBOOT_MESSAGE(kServerNameTooLong, "host.server_name_too_long", "The server name can be at most {limit} bytes");
REBOOT_MESSAGE(kDescriptionTooLong, "host.description_too_long", "The server description can be at most {limit} bytes");
REBOOT_MESSAGE(kProfileStale, "host.profile_stale",
               "The host profile {name} was changed elsewhere; reload it and try again");
REBOOT_MESSAGE(kBuiltinProfile, "host.builtin_profile", "The built-in host profile {name} cannot be deleted");
REBOOT_MESSAGE(kAutoProfileListed, "host.auto_profile_listed",
               "The server started for your own game is always unlisted");
REBOOT_MESSAGE(kInvalidPortPolicy, "host.invalid_port_policy",
               "Host ports must be between {min} and 65535, with the first port of a range below the last");
REBOOT_MESSAGE(kReservedPort, "host.reserved_port", "Port {port} is reserved for the backend");
REBOOT_MESSAGE(kInvalidMatchEndDelay, "host.invalid_match_end_delay",
               "The delay after a match can be at most {limit}");
REBOOT_MESSAGE(kInvalidOperatorAddress, "host.invalid_operator_address",
               "{address} is not an IP address or CIDR block");
REBOOT_MESSAGE(kBanWithoutTarget, "host.ban_without_target",
               "A ban needs an IP address, a CIDR block or an account id");
REBOOT_MESSAGE(kProfileBusy, "host.profile_busy", "The host profile {name} is already running");
REBOOT_MESSAGE(kHostLimitReached, "host.host_limit_reached", "At most {limit} servers can run at the same time");
REBOOT_MESSAGE(kLinkedNeedsAutoProfile, "host.linked_needs_auto_profile",
               "A server started for your own game must use the built-in auto profile");
REBOOT_MESSAGE(kAutoProfileNeedsLink, "host.auto_profile_needs_link",
               "The built-in auto profile only runs a server for your own game");
REBOOT_MESSAGE(kNoBuildSelected, "host.no_build_selected", "Choose a build or a game version to host first");
REBOOT_MESSAGE(kBuildVersionUnknown, "host.build_version_unknown",
               "The game version of {name} is not confirmed, so it cannot be hosted");
REBOOT_MESSAGE(kBlockOutOfRange, "host.block_out_of_range",
               "The game server needs {block_size} ports starting at {port}, which does not fit");
REBOOT_MESSAGE(kNoFreeBlock, "host.no_free_block",
               "No {block_size} free ports are left between {first} and {last}");
REBOOT_MESSAGE(kListenTimeout, "host.listen_timeout", "The game server did not start listening in time");
REBOOT_MESSAGE(kPortNotOwned, "host.port_not_owned",
               "The game server reported port {port}, but another program holds it");
REBOOT_MESSAGE(kReadinessTimeout, "host.readiness_timeout",
               "The game server is not answering on UDP port {port}");
REBOOT_MESSAGE(kNotHostSession, "host.not_host_session", "Session {session} is not a hosted server");
REBOOT_MESSAGE(kServerNotRunning, "host.server_not_running", "The game server is not running right now");
REBOOT_MESSAGE(kNotListening, "host.not_listening", "The game server of session {session} is not listening yet");
REBOOT_MESSAGE(kBuildAndVersion, "host.build_and_version",
               "A host profile hosts either an installed build or a game version, not both");
REBOOT_MESSAGE(kBlockInUse, "host.block_in_use", "Port {port} is already used by another server you host");
REBOOT_MESSAGE(kCancelled, "host.cancelled", "Starting the server was cancelled");
REBOOT_MESSAGE(kUntestedDeclined, "host.untested_declined", "Hosting this untested build was declined");
REBOOT_MESSAGE(kInvalidAnswer, "host.invalid_answer", "The answer to this question must be yes or no");
REBOOT_MESSAGE(kListenFailed, "host.listen_failed", "The game server could not use UDP port {port}");
REBOOT_MESSAGE(kServerExited, "host.server_exited", "The game server exited with code {code} before it was ready");
REBOOT_MESSAGE(kServerFatal, "host.server_fatal", "The game server stopped with error {code}");
REBOOT_MESSAGE(kServerUnresponsive, "host.server_unresponsive", "The game server stopped responding");
REBOOT_MESSAGE(kProfileMemberInvalid, "host.profile_member_invalid", "The host profile member {member} is not valid");
REBOOT_MESSAGE(kDuplicateProfile, "host.duplicate_profile", "The host profile {profile} is listed more than once");
REBOOT_MESSAGE(kSecondAutoServerUnpublished, "host.second_auto_server_unpublished",
               "Another server for your own game is already shared, so this one is not");

}  // namespace reboot::host::msg
