#include "messages.hpp"

namespace rb::secrets::msg {

REBOOT_MESSAGE(kInvalidScope, "secrets.invalid_scope", "{scope} does not identify a {kind}");
REBOOT_MESSAGE(kEmptyValue, "secrets.empty_value", "The {kind} cannot be empty");
REBOOT_MESSAGE(kTooLarge, "secrets.too_large", "The {kind} is longer than {max_bytes} bytes");
REBOOT_MESSAGE(kRetentionNotAllowed, "secrets.retention_not_allowed",
               "Whether a {kind} is saved cannot be chosen");
REBOOT_MESSAGE(kRequestNotPending, "secrets.request_not_pending", "{scope} is not a pending request for a {kind}");
REBOOT_MESSAGE(kNotFound, "secrets.not_found", "No {kind} is set for {scope}");
REBOOT_MESSAGE(kRevealForbidden, "secrets.reveal_forbidden", "A {kind} cannot be read back");
REBOOT_MESSAGE(kNotReady, "secrets.not_ready", "Saved secrets are still loading");
REBOOT_MESSAGE(kStoreUnavailable, "secrets.store_unavailable",
               "No secure store is available in this logon session, so the {kind} is kept only until the engine exits");
REBOOT_MESSAGE(kStoreReadFailed, "secrets.store_read_failed",
               "Saved secrets cannot be read from the secure store; new ones are kept only until the engine exits");
REBOOT_MESSAGE(kStoreWriteFailed, "secrets.store_write_failed",
               "The {kind} cannot be saved in the secure store, so it is kept only until the engine exits");
REBOOT_MESSAGE(kStoreEraseFailed, "secrets.store_erase_failed", "The {kind} cannot be removed from the secure store");
REBOOT_MESSAGE(kStoreTimedOut, "secrets.store_timed_out", "The secure store did not answer within {timeout}");
REBOOT_MESSAGE(kRequestWithdrawn, "secrets.request_withdrawn", "The request for the {kind} was withdrawn");
REBOOT_MESSAGE(kAnswerWithoutSecret, "secrets.answer_without_secret",
               "The request for the {kind} was answered before a new one was provided");

}  // namespace rb::secrets::msg
