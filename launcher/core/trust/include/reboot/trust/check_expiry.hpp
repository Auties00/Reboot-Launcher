#pragma once

#include <chrono>
#include <optional>

#include "reboot/foundation/diag.hpp"
#include "reboot/trust/signed_document_kind.hpp"

namespace reboot::trust {

// A Warning diagnostic once `now` passes `expires_at`. Expiry never rejects: the caller keeps
// the verified (cached) copy in use and surfaces the warning.
[[nodiscard]] std::optional<Diagnostic> check_expiry(SignedDocumentKind kind,
                                                     std::chrono::system_clock::time_point expires_at,
                                                     std::chrono::system_clock::time_point now);

}  // namespace reboot::trust
