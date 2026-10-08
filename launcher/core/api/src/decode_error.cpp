#include "reboot/api/decode_error.hpp"

#include "messages.hpp"

namespace reboot::api {

::reboot::Diagnostic to_diagnostic(DecodeError error, u32 method_id) {
    ::reboot::MessageId message = msg::kMalformedRequest;
    switch (error) {
        case DecodeError::Malformed: message = msg::kMalformedRequest; break;
        case DecodeError::ConflictingCases: message = msg::kConflictingCases; break;
        case DecodeError::UnknownCase: message = msg::kUnknownCase; break;
    }
    return ::reboot::make_diag(::reboot::ErrorDomain::Api, message)
        .arg("method", method_id)
        .kind(::reboot::ErrorKind::InvalidInput);
}

}  // namespace reboot::api
