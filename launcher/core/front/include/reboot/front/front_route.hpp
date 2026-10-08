#pragma once

#include <array>
#include <optional>
#include <variant>

#include "reboot/foundation/types.hpp"
#include "reboot/front/session_key.hpp"
#include "reboot/front/ticket_exchange.hpp"
#include "reboot/front/upstream_origin.hpp"

namespace reboot::front {

// Our backend at set_embedded_backend's port, sent X-Reboot-Session; it mints the game's credential itself.
struct EmbeddedUpstream {};

// A Local or Remote backend. BackendService already asked any plain-http consent for its origin.
struct ConfiguredUpstream {
    UpstreamOrigin origin;
    // Trusted on first use: SHA-256 of a self-signed backend's leaf DER, replacing chain checks on its host.
    std::optional<std::array<u8, 32>> pinned_certificate;
    // The lease's session-only plain-http consent, which also covers relays to the same host.
    bool plain_http_consented = false;
};

// Requests reach it unchanged but for Host, X-Reboot-* and Location; bodies are never decoded for the game.
using RouteUpstream = std::variant<EmbeddedUpstream, ConfiguredUpstream>;

// Capabilities: auth-backend.reverse-proxy.
// What one session's /s/<key>/ prefix reaches. Move-only, since the ticket holds secrets.
struct FrontRoute {
    SessionId session;
    SessionKey key;
    RouteUpstream upstream;
    // Only with a ConfiguredUpstream whose account is password-backed.
    std::optional<TicketExchange> tickets;
};

}  // namespace reboot::front
