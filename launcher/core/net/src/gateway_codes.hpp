#pragma once

#include "reboot/net/gateway_error.hpp"

namespace reboot::net {

// A miniupnpc result: UPnP error codes, or its negative UPNPCOMMAND_* codes.
[[nodiscard]] GatewayError upnp_error(int code);

// A libnatpmp result: its negative NATPMP_ERR_* codes, or a positive NAT-PMP result code.
[[nodiscard]] GatewayError natpmp_error(int code);

}  // namespace reboot::net
