#include "reboot/api/decode_error.hpp"

#include "messages.hpp"

namespace rb::api {

::rb::Diagnostic to_diagnostic(DecodeError error, u32 method_id) {
    ::rb::MessageId message = msg::kMalformedRequest;
    switch (error) {
        case DecodeError::Malformed: message = msg::kMalformedRequest; break;
        case DecodeError::ConflictingCases: message = msg::kConflictingCases; break;
        case DecodeError::UnknownCase: message = msg::kUnknownCase; break;
    }
    return ::rb::make_diag(::rb::ErrorDomain::Api, message)
        .arg("method", method_id)
        .kind(::rb::ErrorKind::InvalidInput);
}

}  // namespace rb::api
