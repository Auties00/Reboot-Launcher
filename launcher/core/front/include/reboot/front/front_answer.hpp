#pragma once

#include "reboot/foundation/types.hpp"

namespace rb::front {

// What the front answers itself instead of relaying; each value is the HTTP status.
enum class FrontAnswer : u16 {
    // Host or Origin refused by LoopbackAuthority, or a relay target the route does not allow.
    Forbidden = 403,
    UnknownKey = 404,
    // Unreachable, refused or unverifiable upstream, or a learned plain-ws origin still awaiting consent.
    BadGateway = 502,
    // The embedded backend is down or restarting.
    BackendDown = 503,
    // No response head within FrontOptions::upstream_response.
    GatewayTimeout = 504,
};

}  // namespace rb::front
