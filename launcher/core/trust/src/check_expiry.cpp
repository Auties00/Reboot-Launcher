#include "reboot/trust/check_expiry.hpp"

#include "messages.hpp"

namespace reboot::trust {

std::optional<Diagnostic> check_expiry(SignedDocumentKind kind, std::chrono::system_clock::time_point expires_at,
                                       std::chrono::system_clock::time_point now) {
    if (now <= expires_at) return std::nullopt;
    return make_diag(ErrorDomain::Trust, kDocumentExpired)
        .arg("document", document_kind_name(kind))
        .arg("expired_for", std::chrono::duration_cast<std::chrono::milliseconds>(now - expires_at))
        .severity(Severity::Warning)
        .build();
}

}  // namespace reboot::trust
