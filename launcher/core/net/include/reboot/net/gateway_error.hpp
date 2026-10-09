#pragma once

#include <optional>
#include <string>

#include "reboot/foundation/types.hpp"

namespace rb::net {

// OnlyPermanentLease is UPnP 725, ExternalPortTaken is UPnP 718.
enum class GatewayErrorCode : u8 {
    NoGateway,
    OnlyPermanentLease,
    ExternalPortTaken,
    Refused,
    Timeout,
    Unsupported,
    Failed,
};

struct GatewayError {
    GatewayErrorCode code = GatewayErrorCode::Failed;
    std::optional<i64> protocol_code;
    std::optional<std::string> detail;
};

}  // namespace rb::net
