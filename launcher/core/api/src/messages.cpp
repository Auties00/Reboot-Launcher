#include "messages.hpp"

namespace rb::api::msg {

REBOOT_MESSAGE(kUnknownMethod, "api.unknown_method", "This engine has no method {method}.");
REBOOT_MESSAGE(kWrongMethodKind, "api.wrong_method_kind",
               "Method {method} was invoked as the wrong kind: call or operation.");
REBOOT_MESSAGE(kMalformedRequest, "api.malformed_request", "The request for method {method} could not be decoded.");
REBOOT_MESSAGE(kConflictingCases, "api.conflicting_cases",
               "The request for method {method} sets more than one alternative of a choice.");
REBOOT_MESSAGE(kUnknownCase, "api.unknown_case",
               "The request for method {method} uses an alternative this engine does not know.");

}  // namespace rb::api::msg
