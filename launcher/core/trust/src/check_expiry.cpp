#include "reboot/trust/check_expiry.hpp"

#include <algorithm>

#include "messages.hpp"

namespace reboot::trust {

std::optional<Diagnostic> check_expiry(SignedDocumentKind kind, std::chrono::system_clock::time_point expires_at,
                                       std::chrono::system_clock::time_point now) {
    using std::chrono::milliseconds;
    if (now <= expires_at) return std::nullopt;
    // Subtracting in milliseconds cannot overflow even for time points centuries apart.
    const milliseconds expired_for = std::chrono::ceil<milliseconds>(now.time_since_epoch()) -
                                     std::chrono::ceil<milliseconds>(expires_at.time_since_epoch());
    return make_diag(ErrorDomain::Trust, kDocumentExpired)
        .arg("document", document_kind_name(kind))
        .arg("expired_for", std::max(expired_for, milliseconds(1)))
        .severity(Severity::Warning)
        .build();
}

}  // namespace reboot::trust
