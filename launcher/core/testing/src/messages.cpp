#include "messages.hpp"

namespace rb::testing::msg {

REBOOT_MESSAGE(kUnscriptedSpawn, "testing.unscripted_spawn", "Nothing is scripted for starting {exe}");
REBOOT_MESSAGE(kNoHttpRoute, "testing.no_http_route", "No fake response is set for {method} {url}");
REBOOT_MESSAGE(kNotFound, "testing.not_found", "{path} does not exist");
REBOOT_MESSAGE(kNotADirectory, "testing.not_a_directory", "{path} is not a directory");
REBOOT_MESSAGE(kLockHeld, "testing.lock_held", "{path} is locked by someone else");
REBOOT_MESSAGE(kLockWaitExceeded, "testing.lock_wait_exceeded", "{path} stayed locked for {limit}");
REBOOT_MESSAGE(kHeldOpen, "testing.held_open", "{path} is held open and cannot be replaced");
REBOOT_MESSAGE(kNoVolume, "testing.no_volume", "No volume contains {path}");
REBOOT_MESSAGE(kUnresolvedHost, "testing.unresolved_host", "{host} has no address");
REBOOT_MESSAGE(kCancelled, "testing.cancelled", "The request was cancelled");
REBOOT_MESSAGE(kSecretStoreUnavailable, "testing.secret_store_unavailable", "The secret store is unavailable");
REBOOT_MESSAGE(kHttpsOnly, "testing.https_only", "Only https addresses can be opened, not {url}");
REBOOT_MESSAGE(kEndpointInUse, "testing.endpoint_in_use", "{endpoint} already has a listener");
REBOOT_MESSAGE(kNoListener, "testing.no_listener", "Nothing listens on {endpoint}");
REBOOT_MESSAGE(kNoSuchProcess, "testing.no_such_process", "Process {pid} does not exist");
REBOOT_MESSAGE(kNoRuntimeLayout, "testing.no_runtime_layout", "No runtime layout is set for runner {runner}");
REBOOT_MESSAGE(kStreamClosed, "testing.stream_closed", "The stream is closed");
REBOOT_MESSAGE(kBadScript, "testing.bad_script", "{path} is not a valid fake script: {reason}");
REBOOT_MESSAGE(kMalformedReply, "testing.malformed_reply",
               "The reply to request {req_id} has neither a payload nor an error");
REBOOT_MESSAGE(kBadBootstrap, "testing.bad_bootstrap", "The game-control environment lacks a valid {name}");
REBOOT_MESSAGE(kIsADirectory, "testing.is_a_directory", "{path} is a directory");
REBOOT_MESSAGE(kIsALink, "testing.is_a_link", "{path} is a symbolic link");
REBOOT_MESSAGE(kSimulatedCrash, "testing.simulated_crash", "Replacing {path} stopped before the rename");
REBOOT_MESSAGE(kScratchDirFailed, "testing.scratch_dir_failed", "Cannot create a scratch directory under {path}");
REBOOT_MESSAGE(kUnknownStream, "testing.unknown_stream", "Stream {stream} was never opened");
REBOOT_MESSAGE(kRenameConflict, "testing.rename_conflict", "{account} already exists");
REBOOT_MESSAGE(kSocketFailed, "testing.socket_failed", "{operation} failed for {endpoint}");

}  // namespace rb::testing::msg
