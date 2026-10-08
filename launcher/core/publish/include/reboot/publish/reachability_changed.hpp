#pragma once

#include <optional>

#include "reboot/foundation/net_types.hpp"
#include "reboot/foundation/types.hpp"
#include "reboot/publish/host_status.hpp"

namespace reboot::publish {

// Capabilities: matchmaking-networking.public-ip.
// EventKind::ReachabilityChanged, coalesced per session; the only source of the probe verdict and
// the public address. Published at HostRegistered, at each rbsb/1 HostStatus, when the advertised
// port changes, and with both fields absent when the registration ends.
struct ReachabilityChanged {
    SessionId session;
    // Absent while not Registered.
    std::optional<HostStatus> status;
    u32 probe_failures = 0;
    // The address the edge observed in HostRegistered, with the advertised port. It replaces
    // ipify: nothing asks a third party. Always IPv4, since the host connection is.
    std::optional<Endpoint> public_endpoint;
};

}  // namespace reboot::publish
