#include "messages.hpp"

namespace reboot::os_windows::ipc {

// core/ipc registers the same id; this package may not reach it.
REBOOT_MESSAGE(kEndpointUntrusted, "ipc.endpoint_untrusted", "The engine endpoint is not trusted.");
REBOOT_MESSAGE(kIpcCallFailed, "platform.ipc_call_failed", "{call} failed.");
REBOOT_MESSAGE(kIpcCallFailedOnPath, "platform.ipc_call_failed_on_path", "{call} failed on {path}.");
REBOOT_MESSAGE(kPipeCallFailed, "platform.pipe_call_failed", "{call} failed on the engine pipe {name}.");
REBOOT_MESSAGE(kPipeNameTaken, "platform.pipe_name_taken", "Another process already holds the engine pipe {name}.");
REBOOT_MESSAGE(kEngineNotListening, "platform.engine_not_listening", "No engine is listening at {name}.");
REBOOT_MESSAGE(kPipeConnectTimedOut, "platform.pipe_connect_timed_out",
               "The engine pipe {name} stayed busy for {deadline}.");
REBOOT_MESSAGE(kPipeAccessDenied, "platform.pipe_access_denied", "The engine pipe {name} refused this user.");
REBOOT_MESSAGE(kPipeOwnerMismatch, "platform.pipe_owner_mismatch",
               "The engine pipe is owned by {owner_sid}, not by {expected_sid}.");
REBOOT_MESSAGE(kPipeServerOtherUser, "platform.pipe_server_other_user",
               "The engine pipe is served by process {pid} running as {server_sid}, not as {expected_sid}.");
REBOOT_MESSAGE(kPipeServerUnverifiable, "platform.pipe_server_unverifiable",
               "The user of process {pid}, which serves the engine pipe, could not be read, so only the "
               "pipe's owner was checked.");
REBOOT_MESSAGE(kPipeClientOtherUser, "platform.pipe_client_other_user",
               "Process {pid} connected to the engine pipe as {client_sid}, not as {expected_sid}.");
REBOOT_MESSAGE(kPipeClientUnidentified, "platform.pipe_client_unidentified",
               "Process {pid} connected to the engine pipe without letting the engine identify its user.");
REBOOT_MESSAGE(kSpawnLockTimedOut, "platform.spawn_lock_timed_out", "{path} stayed locked for {deadline}.");
REBOOT_MESSAGE(kTaskSchedulerTimedOut, "platform.task_scheduler_timed_out",
               "The Task Scheduler did not answer {call} within {deadline}.");
REBOOT_MESSAGE(kEngineSpawnFailed, "platform.engine_spawn_failed", "{path} could not be started.");

}  // namespace reboot::os_windows::ipc
