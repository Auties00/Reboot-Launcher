#pragma once

#include <optional>
#include <string>

#include "reboot/foundation/net_types.hpp"
#include "reboot/foundation/types.hpp"
#include "reboot/identity/login_target.hpp"

namespace reboot::backend {

// Where the front sends a session's backend traffic.
struct BackendUpstream {
    // http://127.0.0.1:<http_port> for Embedded, the probed origin for Local and Remote.
    std::string origin;
    // Reboot, embedded included: the front adds X-Reboot-Xmpp and X-Reboot-Console-Key to every
    // forwarded request, which our backend prefers over its own config.
    identity::UpstreamFlavor flavor = identity::UpstreamFlavor::Reboot;
    // Where the front relays XMPP and matchmaker WebSockets: the embedded listener, the user's
    // XMPP endpoint, or a Reboot upstream's backend-info ws_port. Unset: the auth DLL leaves
    // XMPP connects alone, and a Remote session raises XmppUnavailable.
    std::optional<HostPort> websocket;
    // Embedded only, for Backend.Status. Loopback users can reach it directly, so the embedded
    // backend serves only callers holding a token it issued (see BackendProcess).
    std::optional<u16> http_port;

    bool operator==(const BackendUpstream&) const = default;
};

}  // namespace reboot::backend
