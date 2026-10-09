#include "reboot/browser/join_outcome.hpp"

#include "messages.hpp"
#include "reboot/foundation/text.hpp"

namespace reboot::browser {

Diagnostic to_diagnostic(const JoinFailure& failure) {
    const auto diag = [](MessageId id) { return make_diag(ErrorDomain::Browser, id); };
    switch (failure.code) {
        case JoinFailureCode::OwnServer: return diag(kJoinOwnServer).kind(ErrorKind::InvalidInput);
        case JoinFailureCode::NotFound: return diag(kServerNotFound).kind(ErrorKind::NotFound);
        case JoinFailureCode::Offline: return diag(kServerOffline).retryable();
        case JoinFailureCode::VersionMismatch:
            return diag(kJoinVersionMismatch)
                .arg("version", sanitize_display_text(failure.server_version))
                .arg("local_version", failure.local_version ? failure.local_version->canonical() : std::string())
                .kind(ErrorKind::Conflict);
        case JoinFailureCode::Unreachable: return diag(kServerUnreachable).retryable();
        case JoinFailureCode::WrongPassword: return diag(kWrongPassword).kind(ErrorKind::InvalidInput);
        case JoinFailureCode::TooManyAttempts:
            return diag(kTooManyJoinAttempts).arg("retry_after", failure.retry_after).retryable();
        case JoinFailureCode::UnsupportedAddressFamily:
            return diag(kUnsupportedAddressFamily)
                .arg("address", failure.granted ? failure.granted->to_string() : std::string())
                .kind(ErrorKind::Unsupported);
        case JoinFailureCode::Refused: return diag(kJoinRefused).kind(ErrorKind::Cancelled);
        case JoinFailureCode::EdgeUnavailable: break;
    }
    DiagBuilder builder = diag(kEdgeUnavailable).retryable();
    if (failure.cause) std::move(builder).cause(*failure.cause);
    return std::move(builder).build();
}

}  // namespace reboot::browser
