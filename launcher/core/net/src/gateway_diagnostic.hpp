#pragma once

#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/net_types.hpp"
#include "reboot/net/gateway_error.hpp"

namespace reboot::net {

// net.no_gateway for NoGateway, otherwise net.mapping_refused on the game's first port.
[[nodiscard]] Diagnostic gateway_diagnostic(const GatewayError& error, Port port = Port{});

// `message` is net.mapping_refused or net.mapping_renew_failed.
[[nodiscard]] Diagnostic mapping_failure(MessageId message, Port port, const GatewayError& error);

}  // namespace reboot::net
