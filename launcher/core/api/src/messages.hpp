#pragma once

#include "reboot/foundation/diag.hpp"

// Each carries the method id as the "method" arg.
namespace rb::api::msg {

REBOOT_MESSAGE_DECL(kUnknownMethod);
REBOOT_MESSAGE_DECL(kWrongMethodKind);
REBOOT_MESSAGE_DECL(kMalformedRequest);
REBOOT_MESSAGE_DECL(kConflictingCases);
REBOOT_MESSAGE_DECL(kUnknownCase);

}  // namespace rb::api::msg
