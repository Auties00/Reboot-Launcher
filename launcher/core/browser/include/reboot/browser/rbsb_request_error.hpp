#pragma once

#include <chrono>
#include <optional>
#include <string>

#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/types.hpp"
#include "wire/messages.hpp"

namespace reboot::browser {

// Rejected carries the edge's Error code.
enum class RbsbFailure : u8 { Rejected, NotConnected, ConnectionLost, TimedOut, Cancelled };

struct RbsbRequestError {
    RbsbFailure failure = RbsbFailure::ConnectionLost;
    sb::wire::ErrorCode code = sb::wire::ErrorCode::unknown;
    // Untrusted edge text; kept for the log only.
    std::string message;
    std::chrono::milliseconds retry_after{0};
    // NotConnected: why the last connect attempt failed, when one did.
    std::optional<Diagnostic> cause;
};

// One message per edge ErrorCode and failure; NotConnected yields `cause` when set.
[[nodiscard]] Diagnostic to_diagnostic(const RbsbRequestError& error);

}  // namespace reboot::browser
