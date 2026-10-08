#include "reboot/trust/serial_guard.hpp"

#include <utility>

namespace reboot::trust {

SerialGuard::SerialGuard(SignedDocumentKind kind, u64 highest_seen, Persist persist)
    : kind_(kind), highest_seen_(highest_seen), persist_(std::move(persist)) {}

std::expected<SerialCheck, TrustError> SerialGuard::admit(u64 serial) {
    if (serial < highest_seen_)
        return std::unexpected(TrustError{
            .code = TrustErrorCode::SerialRollback, .document = kind_, .serial = serial, .highest_seen = highest_seen_});
    if (serial == highest_seen_) return SerialCheck::Same;

    if (auto persisted = persist_(serial); !persisted)
        return std::unexpected(TrustError{.code = TrustErrorCode::SerialPersistFailed,
                                          .document = kind_,
                                          .serial = serial,
                                          .highest_seen = highest_seen_,
                                          .cause = std::move(persisted.error())});
    highest_seen_ = serial;
    return SerialCheck::Advanced;
}

}  // namespace reboot::trust
