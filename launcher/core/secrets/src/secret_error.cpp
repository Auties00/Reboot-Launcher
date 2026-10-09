#include "reboot/secrets/secret_error.hpp"

#include "messages.hpp"

namespace rb::secrets {

namespace {

MessageId message_for(SecretError error) noexcept {
    switch (error) {
        case SecretError::InvalidScope: return msg::kInvalidScope;
        case SecretError::EmptyValue: return msg::kEmptyValue;
        case SecretError::TooLarge: return msg::kTooLarge;
        case SecretError::RetentionNotAllowed: return msg::kRetentionNotAllowed;
        case SecretError::RequestNotPending: return msg::kRequestNotPending;
        case SecretError::NotFound: return msg::kNotFound;
        case SecretError::RevealForbidden: return msg::kRevealForbidden;
        case SecretError::NotReady: return msg::kNotReady;
        case SecretError::StoreUnavailable: return msg::kStoreUnavailable;
        case SecretError::StoreReadFailed: return msg::kStoreReadFailed;
        case SecretError::StoreWriteFailed: return msg::kStoreWriteFailed;
        case SecretError::StoreEraseFailed: return msg::kStoreEraseFailed;
        case SecretError::StoreTimedOut: return msg::kStoreTimedOut;
        case SecretError::RequestWithdrawn: return msg::kRequestWithdrawn;
        case SecretError::AnswerWithoutSecret: return msg::kAnswerWithoutSecret;
    }
    return {};
}

}  // namespace

bool has_error(const Diagnostic& diag, SecretError error) noexcept {
    return diag.domain == ErrorDomain::Secrets && diag.is(message_for(error));
}

}  // namespace rb::secrets
