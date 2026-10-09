#include "gateway_codes.hpp"

namespace reboot::net {

GatewayError upnp_error(int code) {
    GatewayError error;
    error.protocol_code = code;
    switch (code) {
        case 718: error.code = GatewayErrorCode::ExternalPortTaken; break;
        case 725: error.code = GatewayErrorCode::OnlyPermanentLease; break;
        // Not authorized, wildcard-only, no ports left, or held by another mechanism.
        case 606:
        case 724:
        case 726:
        case 727:
        case 728:
        case 729: error.code = GatewayErrorCode::Refused; break;
        // 401 Invalid Action: the IGD lacks the call.
        case 401:
        case 602: error.code = GatewayErrorCode::Unsupported; break;
        // UPNPCOMMAND_HTTP_ERROR: the IGD did not answer the SOAP request.
        case -3: error.code = GatewayErrorCode::Timeout; break;
        default: error.code = GatewayErrorCode::Failed; break;
    }
    return error;
}

GatewayError natpmp_error(int code) {
    GatewayError error;
    error.protocol_code = code;
    switch (code) {
        // NATPMP_ERR_CANNOTGETGATEWAY, NATPMP_ERR_NOGATEWAYSUPPORT
        case -3:
        case -7: error.code = GatewayErrorCode::NoGateway; break;
        // NATPMP_ERR_NOTAUTHORIZED, NATPMP_ERR_OUTOFRESOURCES, and result codes 2 and 4
        case -51:
        case -53:
        case 2:
        case 4: error.code = GatewayErrorCode::Refused; break;
        // NATPMP_ERR_UNSUPPORTEDVERSION, NATPMP_ERR_UNSUPPORTEDOPCODE, and result codes 1 and 5
        case -14:
        case -15:
        case 1:
        case 5: error.code = GatewayErrorCode::Unsupported; break;
        default: error.code = GatewayErrorCode::Failed; break;
    }
    return error;
}

}  // namespace reboot::net
