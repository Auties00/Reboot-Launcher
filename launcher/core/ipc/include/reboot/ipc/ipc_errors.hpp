#pragma once

#include "reboot/foundation/diag.hpp"

// The one declaration of every ipc.* id; reboot_client and the OS ipc packages use these.
namespace reboot::ipc {

// {field}: "user_id" or "root_hash16".
REBOOT_MESSAGE_DECL(kInvalidEndpointInput);
// The endpoint's owner is not the caller's user.
REBOOT_MESSAGE_DECL(kEndpointUntrusted);
// {method}, {engine_build}
REBOOT_MESSAGE_DECL(kVersionMismatch);
// {limit}
REBOOT_MESSAGE_DECL(kTooManyCalls);
// {limit}
REBOOT_MESSAGE_DECL(kTooManySubscriptions);
// {sub}
REBOOT_MESSAGE_DECL(kUnknownSubscription);
// {sub}
REBOOT_MESSAGE_DECL(kDuplicateSubscription);
// {op}
REBOOT_MESSAGE_DECL(kUnknownOp);
// {frame_type}
REBOOT_MESSAGE_DECL(kProtocolError);
REBOOT_MESSAGE_DECL(kRevealRefused);
// {size}, {limit}: a message whose frame would exceed kIpcFrameCap.
REBOOT_MESSAGE_DECL(kMessageTooLarge);

// ErrorKind::EngineUnavailable.
REBOOT_MESSAGE_DECL(kConnectionLost);
// {reason}
REBOOT_MESSAGE_DECL(kEngineClosed);
// {deadline}
REBOOT_MESSAGE_DECL(kEngineUnavailable);
// {deadline}
REBOOT_MESSAGE_DECL(kUpdateInProgress);
REBOOT_MESSAGE_DECL(kEngineCannotDetach);
REBOOT_MESSAGE_DECL(kElevatedAutostartRefused);
REBOOT_MESSAGE_DECL(kNoInteractiveSession);
REBOOT_MESSAGE_DECL(kAgentRequiresApproval);
// {engine_root}, {root}
REBOOT_MESSAGE_DECL(kRootMismatch);
// Severity::Warning. {image_path}, {expected_path}
REBOOT_MESSAGE_DECL(kEngineImageDiffers);

}  // namespace reboot::ipc
