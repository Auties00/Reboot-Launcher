#pragma once

#include <expected>

#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/function.hpp"
#include "reboot/foundation/types.hpp"
#include "reboot/trust/signed_document_kind.hpp"
#include "reboot/trust/trust_error.hpp"

namespace rb::trust {

enum class SerialCheck : u8 { Same, Advanced };

// Refuses a verified document whose serial is below the highest one seen for its kind.
// Decisions: archive-extraction-download (catalog), update-mechanism (manifest rollback is a
// serial-bumped manifest, never a lower serial).
class SerialGuard {
public:
    // Called with the new highest serial before it takes effect; the caller owns the store.
    using Persist = UniqueFunction<Result<void>(u64 serial)>;

    SerialGuard(SignedDocumentKind kind, u64 highest_seen, Persist persist);

    // Only for a document that already passed verify_signed. A higher serial is persisted
    // first and is not admitted if persisting fails.
    [[nodiscard]] std::expected<SerialCheck, TrustError> admit(u64 serial);

    [[nodiscard]] u64 highest_seen() const noexcept { return highest_seen_; }

private:
    SignedDocumentKind kind_;
    u64 highest_seen_;
    Persist persist_;
};

}  // namespace rb::trust
