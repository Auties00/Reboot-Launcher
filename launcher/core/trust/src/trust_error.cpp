#include "reboot/trust/trust_error.hpp"

#include <utility>

#include "messages.hpp"

namespace reboot::trust {

Diagnostic to_diagnostic(const TrustError& error) {
    const std::string_view document = document_kind_name(error.document);
    switch (error.code) {
        case TrustErrorCode::SignatureMalformed:
            return make_diag(ErrorDomain::Trust, kSignatureMalformed).arg("document", document).build();
        case TrustErrorCode::UnknownKey:
            return make_diag(ErrorDomain::Trust, kUnknownKey)
                .arg("document", document)
                .arg("key_id", error.key_id)
                .build();
        case TrustErrorCode::SignatureInvalid:
            return make_diag(ErrorDomain::Trust, kSignatureInvalid)
                .arg("document", document)
                .arg("key_id", error.key_id)
                .build();
        case TrustErrorCode::SerialRollback:
            return make_diag(ErrorDomain::Trust, kSerialRollback)
                .arg("document", document)
                .arg("serial", error.serial)
                .arg("highest_seen", error.highest_seen)
                .kind(ErrorKind::Conflict)
                .build();
        case TrustErrorCode::SerialPersistFailed: {
            auto diag = make_diag(ErrorDomain::Trust, kSerialPersistFailed)
                            .arg("document", document)
                            .arg("serial", error.serial)
                            .retryable();
            if (error.cause) return std::move(diag).cause(*error.cause).build();
            return std::move(diag).build();
        }
        case TrustErrorCode::CryptoFailure: {
            auto diag = make_diag(ErrorDomain::Trust, kCryptoFailure).arg("document", document);
            if (error.detail) return std::move(diag).detail(*error.detail).build();
            return std::move(diag).build();
        }
    }
    return internal_bug("trust::to_diagnostic");
}

}  // namespace reboot::trust
