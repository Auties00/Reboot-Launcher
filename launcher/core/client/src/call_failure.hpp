#pragma once

#include <expected>

#include "reboot/client.h"
#include "reboot/contracts/common.hpp"
#include "reboot/foundation/diag.hpp"

namespace rb::client {

// A failed request. A remote Diagnostic is the engine's Reply.error, passed through unchanged.
struct CallFailure {
    contracts::common::WireDiagnostic diagnostic;
    bool remote = false;
};

template <class T>
using CallResult = std::expected<T, CallFailure>;

[[nodiscard]] CallFailure local_failure(const Diagnostic& diagnostic);

// reboot/client.h's table: remote is RB_E_REMOTE, local maps by id, then by ErrorKind.
[[nodiscard]] rb_status status_for(const CallFailure& failure) noexcept;
// As status_for, except that play refusing a caller in another OS session is RB_E_ENGINE_OTHER_SESSION.
[[nodiscard]] rb_status start_status_for(const CallFailure& failure) noexcept;

}  // namespace rb::client
