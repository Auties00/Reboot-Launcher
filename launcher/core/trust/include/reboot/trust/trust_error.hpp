#pragma once

#include <optional>
#include <string>

#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/types.hpp"
#include "reboot/trust/signed_document_kind.hpp"

namespace rb::trust {

enum class TrustErrorCode : u8 {
    SignatureMalformed,
    UnknownKey,
    SignatureInvalid,
    SerialRollback,
    SerialPersistFailed,
    CryptoFailure,
};

struct TrustError {
    TrustErrorCode code = TrustErrorCode::SignatureInvalid;
    SignedDocumentKind document = SignedDocumentKind::BuildCatalog;
    std::string key_id;
    u64 serial = 0;
    u64 highest_seen = 0;
    // OpenSSL's reason for CryptoFailure.
    std::optional<std::string> detail;
    // The persist callback's failure for SerialPersistFailed.
    std::optional<Diagnostic> cause;
};

[[nodiscard]] Diagnostic to_diagnostic(const TrustError& error);

}  // namespace rb::trust
