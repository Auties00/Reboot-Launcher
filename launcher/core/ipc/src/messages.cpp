#include "reboot/ipc/ipc_errors.hpp"

namespace reboot::ipc {

REBOOT_MESSAGE(kInvalidEndpointInput, "ipc.invalid_endpoint_input",
               "The engine endpoint cannot be named from this {field}.");
REBOOT_MESSAGE(kEndpointUntrusted, "ipc.endpoint_untrusted", "The engine endpoint is not trusted.");
REBOOT_MESSAGE(kVersionMismatch, "ipc.version_mismatch",
               "The running engine ({engine_build}) is a different build and cannot serve method {method}.");
REBOOT_MESSAGE(kTooManyCalls, "ipc.too_many_calls", "More than {limit} requests are waiting for the engine.");
REBOOT_MESSAGE(kTooManySubscriptions, "ipc.too_many_subscriptions",
               "A connection can hold at most {limit} event subscriptions.");
REBOOT_MESSAGE(kUnknownSubscription, "ipc.unknown_subscription", "There is no event subscription {sub}.");
REBOOT_MESSAGE(kDuplicateSubscription, "ipc.duplicate_subscription", "Event subscription {sub} already exists.");
REBOOT_MESSAGE(kUnknownOp, "ipc.unknown_op", "There is no operation {op}.");
REBOOT_MESSAGE(kProtocolError, "ipc.protocol_error",
               "The engine connection received an unexpected frame {frame_type}.");
REBOOT_MESSAGE(kRevealRefused, "ipc.reveal_refused", "Only the host join password can be read back.");

REBOOT_MESSAGE(kConnectionLost, "ipc.connection_lost", "The connection to the engine was lost.");
REBOOT_MESSAGE(kEngineClosed, "ipc.engine_closed", "The engine closed the connection ({reason}).");
REBOOT_MESSAGE(kEngineUnavailable, "ipc.engine_unavailable", "The engine did not answer within {deadline}.");
REBOOT_MESSAGE(kUpdateInProgress, "ipc.update_in_progress",
               "An update is still replacing the engine after {deadline}.");
REBOOT_MESSAGE(kEngineCannotDetach, "ipc.engine_cannot_detach",
               "The engine cannot run on its own from here; start it with reboot-engine run --foreground.");
REBOOT_MESSAGE(kElevatedAutostartRefused, "ipc.elevated_autostart_refused",
               "The engine is not started from an elevated process.");
REBOOT_MESSAGE(kNoInteractiveSession, "ipc.no_interactive_session",
               "The engine is only started from an interactive session.");
REBOOT_MESSAGE(kAgentRequiresApproval, "ipc.agent_requires_approval",
               "The engine needs approval; open the app once to allow it.");
REBOOT_MESSAGE(kRootMismatch, "ipc.root_mismatch", "The engine serves {engine_root}, not {root}.");
REBOOT_MESSAGE(kEngineImageDiffers, "ipc.engine_image_differs",
               "The engine runs from {image_path}, not {expected_path}.");

}  // namespace reboot::ipc
