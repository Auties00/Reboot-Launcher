#pragma once

#include <any>
#include <memory>
#include <optional>
#include <span>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "reboot/foundation/cancel.hpp"
#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/types.hpp"
#include "reboot/front/front_route.hpp"
#include "reboot/front/peer_user.hpp"
#include "reboot/front/ticket_exchange.hpp"
#include "reboot/front/upstream_policy.hpp"
#include "route_table.hpp"

namespace rb {
class UserRequestRegistry;
}

namespace rb::net {
class HostTlsMemory;
}

namespace rb::front {

class FrontConnection;

// Strand-only owner of every route's mutable state; each change republishes the route into the table.
class StrandRoutes : public std::enable_shared_from_this<StrandRoutes> {
public:
    StrandRoutes(RouteTable& table, net::HostTlsMemory& tls, UserRequestRegistry& requests);

    // front.route_exists, key_in_use, or HostTlsMemory's refusal of a plain-http origin.
    [[nodiscard]] Result<void> add(FrontRoute route);
    // Withdraws the route's prompts and hands back its connections.
    [[nodiscard]] std::vector<std::shared_ptr<FrontConnection>> remove(SessionId session);
    [[nodiscard]] bool contains(SessionId session) const;

    // Refused once the route is gone, so a ticket never travels on a dead route.
    [[nodiscard]] TicketSwap swap(SessionId session, std::span<const u8> form, PeerUser peer);
    void settle(SessionId session, std::optional<u32> upstream_status);
    void learn(SessionId session, std::string_view path, std::span<const u8> body);
    void remember_https(std::string_view host);

private:
    struct RouteState {
        FrontRoute route;
        std::optional<UpstreamPolicy> policy;
        std::vector<UpstreamOrigin> consented;
        std::vector<UpstreamOrigin> declined;
        CancelSource prompts;
    };

    void publish(const RouteState& state);
    // A learned plain-ws origin: allowed, refused, or asked about.
    void consider(RouteState& state, const UpstreamOrigin& origin);
    [[nodiscard]] Result<void> answered(SessionId session, const UpstreamOrigin& origin, const std::any& answer);

    RouteTable& table_;
    net::HostTlsMemory& tls_;
    UserRequestRegistry& requests_;
    std::unordered_map<SessionId, RouteState> routes_;
};

}  // namespace rb::front
