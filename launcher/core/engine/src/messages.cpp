#include "messages.hpp"

namespace rb::engine::msg {

REBOOT_MESSAGE(kBadCommandLine, "engine.bad_command_line", "The engine does not accept the argument {argument}");
REBOOT_MESSAGE(kLockFailed, "engine.lock_failed", "Could not take the engine lock {path}");
REBOOT_MESSAGE(kNotReplaceable, "engine.not_replaceable",
               "This engine was started {origin} and cannot be replaced by another build");
REBOOT_MESSAGE(kBusy, "engine.busy", "The engine is still running sessions or operations and cannot be replaced now");
REBOOT_MESSAGE(kShuttingDown, "engine.shutting_down", "The engine is shutting down and starts nothing new");
REBOOT_MESSAGE(kAlreadyDraining, "engine.already_draining", "The engine is already draining for {reason}");
REBOOT_MESSAGE(kExitPending, "engine.exit_pending", "The engine already waits until it is idle to {exit}");
REBOOT_MESSAGE(kAnswerMismatch, "engine.answer_mismatch",
               "The answer does not fit request {request}, which asks for {kind}");
REBOOT_MESSAGE(kConflictingPlayTarget, "engine.conflicting_play_target",
               "A play request cannot name the automatic server together with a server or address");
REBOOT_MESSAGE(kNotReady, "engine.not_ready", "The engine is still reading {what}; try again in a moment");
REBOOT_MESSAGE(kUnknownView, "engine.unknown_view", "There is no open server list view {view}");
REBOOT_MESSAGE(kUnknownNotice, "engine.unknown_notice", "There is no notice {kind} to dismiss");
REBOOT_MESSAGE(kInvalidRequest, "engine.invalid_request", "The request field {field} is not valid");
REBOOT_MESSAGE(kImportNeedsShippingChoice, "engine.import_needs_shipping_choice",
               "{folder} holds {count} game executables; import the folder of the one to use");
REBOOT_MESSAGE(kInstallNotRegistered, "engine.install_not_registered",
               "The build was installed to {folder} but could not be added to the library");
REBOOT_MESSAGE(kEndpointFailed, "engine.endpoint_failed", "The engine could not open its endpoint {endpoint}");
REBOOT_MESSAGE(kWriteFailed, "engine.write_failed", "The engine could not save {document}");
REBOOT_MESSAGE(kCancelled, "engine.cancelled", "The {what} was cancelled");
REBOOT_MESSAGE(kNoWineRunner, "engine.no_wine_runner", "This system runs the game natively and has no Wine runner");
REBOOT_MESSAGE(kSelfTestFailed, "engine.self_test_failed", "The engine could not reach itself at {endpoint}");

}  // namespace rb::engine::msg
