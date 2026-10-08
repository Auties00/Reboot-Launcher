#pragma once

#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/types.hpp"

namespace reboot::secrets {

enum class SecretError : u8 {
    InvalidScope,
    EmptyValue,
    TooLarge,
    RetentionNotAllowed,
    RequestNotPending,
    NotFound,
    RevealForbidden,
    NotReady,
    StoreUnavailable,
    StoreReadFailed,
    StoreWriteFailed,
    StoreEraseFailed,
    StoreTimedOut,
    RequestWithdrawn,
    AnswerWithoutSecret,
};

// True when `diag` is the secrets.* diagnostic for `error`.
[[nodiscard]] bool has_error(const Diagnostic& diag, SecretError error) noexcept;

}  // namespace reboot::secrets
