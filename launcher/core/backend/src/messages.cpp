#include "messages.hpp"

namespace reboot::backend::msg {

REBOOT_MESSAGE(kInvalidUrl, "backend.invalid_url", "{url} is not a backend address");
REBOOT_MESSAGE(kInvalidPort, "backend.invalid_port", "{port} is not a port between 1 and 65535");
REBOOT_MESSAGE(kShuttingDown, "backend.shutting_down", "The launcher is shutting down");
REBOOT_MESSAGE(kInUse, "backend.in_use", "The backend is in use by {sessions} running sessions");
REBOOT_MESSAGE(kEmbeddedOnly, "backend.embedded_only", "This needs the launcher's own backend");
REBOOT_MESSAGE(kLeaseReleased, "backend.lease_released", "The session no longer uses the backend");
REBOOT_MESSAGE(kNotReady, "backend.not_ready", "The backend was not ready within {seconds} seconds");
REBOOT_MESSAGE(kCrashed, "backend.crashed", "The backend stopped unexpectedly");
REBOOT_MESSAGE(kRestartLimitReached, "backend.restart_limit_reached",
               "The backend stopped unexpectedly {restarts} times in {minutes} minutes and was not restarted");
REBOOT_MESSAGE(kUnreachable, "backend.unreachable", "The backend at {origin} does not answer");
REBOOT_MESSAGE(kUnencryptedUpstreamDeclined, "backend.unencrypted_upstream_declined",
               "The unencrypted connection to {host} was not allowed");
REBOOT_MESSAGE(kAccountsNotLoaded, "backend.accounts_not_loaded", "Start the backend to see its accounts");
REBOOT_MESSAGE(kRemoteLoginFailed, "backend.remote_login_failed", "Logging in to {origin} failed with status {status}");
REBOOT_MESSAGE(kExchangeCodeFailed, "backend.exchange_code_failed",
               "{origin} did not issue a login code (status {status})");
REBOOT_MESSAGE(kReconfiguring, "backend.reconfiguring", "The backend is switching to new settings; try again shortly");
REBOOT_MESSAGE(kRemoteAddressMissing, "backend.remote_address_missing", "No remote backend address is set");
REBOOT_MESSAGE(kCancelled, "backend.cancelled", "Waiting for the backend was cancelled");
REBOOT_MESSAGE(kStoppedBeforeReady, "backend.stopped_before_ready", "The backend was stopped before it was ready");
REBOOT_MESSAGE(kAnswerInvalid, "backend.answer_invalid", "That answer does not settle the question");
REBOOT_MESSAGE(kPathNotUtf8, "backend.path_not_utf8", "{path} cannot be passed to the backend");

}  // namespace reboot::backend::msg
