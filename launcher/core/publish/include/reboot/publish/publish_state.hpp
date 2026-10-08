#pragma once

#include <optional>

#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/net_types.hpp"
#include "reboot/foundation/types.hpp"

namespace reboot::publish {

// Retrying: the edge is unreachable, refused for now, or sent GoAway; the session keeps running
// "not listed" and reconnects with backoff. Superseded: the same id was registered from another
// connection (CONFLICT). Refused: the edge rejected the entry or an operation for good (bad field,
// per-address limit, UNAUTHORIZED outside our HostRegister).
enum class PublishPhase : u8 { Connecting, Registering, Registered, Retrying, Superseded, Refused, Withdrawing };

// The edge's probe verdict and the public address are in ReachabilityChanged, their only source.
struct PublishState {
    PublishPhase phase = PublishPhase::Connecting;
    ServerId server;
    // hidden_on_edge() of the last value sent.
    bool hidden = true;
    // The granted external port, or the bound port when nothing was mapped.
    Port advertised_port;
    // Why the phase is Retrying, Superseded or Refused.
    std::optional<Diagnostic> error;
};

}  // namespace reboot::publish
