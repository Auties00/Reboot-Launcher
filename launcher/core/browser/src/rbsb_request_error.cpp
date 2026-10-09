#include "reboot/browser/rbsb_request_error.hpp"

#include "messages.hpp"
#include "reboot/foundation/text.hpp"

namespace rb::browser {

namespace {

[[nodiscard]] DiagBuilder rejected(const RbsbRequestError& error) {
    using sb::wire::ErrorCode;
    const auto diag = [](MessageId id) { return make_diag(ErrorDomain::Browser, id); };
    switch (error.code) {
        case ErrorCode::bad_request:
        case ErrorCode::unauthorized: return diag(kRequestInvalid).kind(ErrorKind::InvalidInput);
        case ErrorCode::unsupported: return diag(kRequestUnsupported).kind(ErrorKind::Unsupported);
        case ErrorCode::rate_limited: return diag(kRateLimited).arg("retry_after", error.retry_after).retryable();
        case ErrorCode::not_found: return diag(kServerNotFound).kind(ErrorKind::NotFound);
        case ErrorCode::wrong_password: return diag(kWrongPassword).kind(ErrorKind::InvalidInput);
        case ErrorCode::unreachable: return diag(kServerUnreachable).retryable();
        case ErrorCode::limit_exceeded: return diag(kTooManyViews).kind(ErrorKind::Conflict);
        case ErrorCode::conflict:
        case ErrorCode::unavailable: return diag(kEdgeUnavailable).retryable();
        case ErrorCode::internal:
        case ErrorCode::unknown: break;
    }
    return diag(kEdgeInternalError).retryable();
}

}  // namespace

Diagnostic to_diagnostic(const RbsbRequestError& error) {
    switch (error.failure) {
        case RbsbFailure::Rejected: {
            DiagBuilder builder = rejected(error);
            if (!error.message.empty()) std::move(builder).detail(normalize_detail(error.message));
            return std::move(builder).build();
        }
        case RbsbFailure::NotConnected:
            if (error.cause) return *error.cause;
            return make_diag(ErrorDomain::Browser, kNotConnected).retryable();
        case RbsbFailure::ConnectionLost: return make_diag(ErrorDomain::Browser, kConnectionLost).retryable();
        case RbsbFailure::TimedOut: return make_diag(ErrorDomain::Browser, kRequestTimeout).retryable();
        case RbsbFailure::Cancelled: return make_diag(ErrorDomain::Browser, kRequestCancelled).kind(ErrorKind::Cancelled);
    }
    return make_diag(ErrorDomain::Browser, kConnectionLost).retryable();
}

}  // namespace rb::browser
