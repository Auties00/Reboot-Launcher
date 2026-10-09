#include "gateway_diagnostic.hpp"

#include <string>

#include "messages.hpp"
#include "reboot/foundation/text.hpp"

namespace rb::net {

Diagnostic gateway_diagnostic(const GatewayError& error, Port port) {
    if (error.code == GatewayErrorCode::NoGateway) {
        DiagBuilder builder = make_diag(ErrorDomain::Net, kNoGateway).severity(Severity::Warning);
        if (error.detail) std::move(builder).detail(normalize_detail(*error.detail));
        return std::move(builder).build();
    }
    return mapping_failure(kMappingRefused, port, error);
}

Diagnostic mapping_failure(MessageId message, Port port, const GatewayError& error) {
    DiagBuilder builder = make_diag(ErrorDomain::Net, message)
                              .arg("protocol", std::string("UDP"))
                              .arg("port", port.value)
                              .arg("gateway_error", error.code)
                              .severity(Severity::Warning);
    if (error.code == GatewayErrorCode::Timeout) std::move(builder).retryable();
    if (error.protocol_code) std::move(builder).arg("protocol_code", *error.protocol_code);
    if (error.detail) std::move(builder).detail(normalize_detail(*error.detail));
    return std::move(builder).build();
}

}  // namespace rb::net
