#pragma once

#include <chrono>

#include "reboot/foundation/operation.hpp"
#include "reboot/foundation/types.hpp"
#include "reboot/net/udp_beacon_prober.hpp"

namespace rb::host {

// Applies only once the server reported Listening. KeepRunningUnpublished leaves a slow or
// unreachable server up as LiveUnpublished, with a diagnostic that says it is not answering
// rather than that it crashed.
enum class ReadinessTimeoutAction : u8 { KeepRunningUnpublished, StopSession };

// The deadline runs from each spawn and covers, in order: Listening, the port ownership check
// and the loopback probe. A server with no Listening by then has no ports to publish, so it is
// stopped and the start fails with host.listen_timeout whatever `on_timeout` says. Port mapping
// and publishing come after the deadline and never fail the session.
// Covers matchmaking-networking.+22.
struct ReadinessPolicy {
    std::chrono::milliseconds deadline = default_deadline(OpKind::HostReadiness);
    // One probe of the game port on 127.0.0.1; never our public address, which needs hairpin NAT.
    net::ProbePolicy loopback_probe;
    ReadinessTimeoutAction on_timeout = ReadinessTimeoutAction::KeepRunningUnpublished;
};

}  // namespace rb::host
