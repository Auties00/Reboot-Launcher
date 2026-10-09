#include "messages.hpp"

namespace reboot::posix {

REBOOT_MESSAGE(kEndpointUntrusted, "ipc.endpoint_untrusted", "The engine endpoint is not trusted.");
REBOOT_MESSAGE(kCallFailed, "posix.call_failed", "{call} failed.");
REBOOT_MESSAGE(kCallFailedOnPath, "posix.call_failed_on_path", "{call} failed on {path}.");
REBOOT_MESSAGE(kSocketPathTooLong, "posix.socket_path_too_long",
               "The engine socket path {path} needs {length} bytes, but the system allows {limit}.");
REBOOT_MESSAGE(kNotADirectory, "posix.not_a_directory", "{path} is not a directory.");
REBOOT_MESSAGE(kDirectoryNotPrivate, "posix.directory_not_private",
               "{path} must be owned by uid {expected_uid} with mode 0700, but it is owned by uid {owner_uid} "
               "with mode {mode}.");
REBOOT_MESSAGE(kPeerOtherUser, "posix.peer_other_user",
               "The process at the other end of the engine socket runs as uid {peer_uid}, not uid {expected_uid}.");
REBOOT_MESSAGE(kEngineNotListening, "posix.engine_not_listening", "No engine is listening at {path}.");
REBOOT_MESSAGE(kLockBusy, "posix.lock_busy", "{path} is locked by another process.");
REBOOT_MESSAGE(kHeldFileChanged, "posix.held_file_changed", "{path} changed while it was held open.");
REBOOT_MESSAGE(kEndpointInUse, "posix.endpoint_in_use", "Another engine is already listening at {path}.");

}  // namespace reboot::posix
