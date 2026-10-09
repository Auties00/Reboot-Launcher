#pragma once

#include <array>
#include <memory>
#include <mutex>
#include <optional>
#include <unordered_map>
#include <vector>

#include "reboot/foundation/function.hpp"
#include "reboot/foundation/net_types.hpp"
#include "reboot/foundation/types.hpp"
#include "reboot/front/session_key.hpp"
#include "reboot/front/upstream_origin.hpp"
#include "reboot/front/upstream_policy.hpp"

namespace rb::front {

class FrontConnection;

enum class ListenerKind : u8 { Session, Fixed };

// A Local or Remote route as connections see it.
struct ConfiguredView {
    UpstreamPolicy policy;
    std::optional<std::array<u8, 32>> pin;
    bool plain_http_consented = false;
    // Learned plain-ws origins, by the answer they got; any other one still awaits it.
    std::vector<UpstreamOrigin> consented;
    std::vector<UpstreamOrigin> declined;
};

// One route as the I/O side reads it; the strand replaces it whole and never mutates a published one.
struct RouteSnapshot {
    SessionId session;
    SessionKey key;
    // nullopt: the embedded backend.
    std::optional<ConfiguredView> configured;
    bool tickets = false;
};

enum class RelayVerdict : u8 { Allowed, Forbidden, AwaitingConsent };

[[nodiscard]] RelayVerdict relay_verdict(const RouteSnapshot& route, const UpstreamOrigin& target);

// The pin applies to the backend's host, relays to it included.
[[nodiscard]] std::optional<std::array<u8, 32>> pin_for(const RouteSnapshot& route, const UpstreamOrigin& target);

// What the strand publishes and the I/O side reads, behind one mutex, plus the live connections, so a
// lookup and its registration are atomic with a route's removal.
class RouteTable {
public:
    void put(std::shared_ptr<const RouteSnapshot> route);
    // Hands back the connections that served the route, for the caller to close.
    [[nodiscard]] std::vector<std::shared_ptr<FrontConnection>> erase(SessionId session);

    [[nodiscard]] std::shared_ptr<const RouteSnapshot> claim(const SessionKey& key, const FrontConnection& connection);
    [[nodiscard]] std::shared_ptr<const RouteSnapshot> claim_legacy(const FrontConnection& connection);

    // Clearing it hands back the fixed-listener connections.
    [[nodiscard]] std::vector<std::shared_ptr<FrontConnection>> set_legacy(std::optional<SessionId> session,
                                                                         std::vector<Port> ports);

    void set_embedded(std::optional<Port> port);
    [[nodiscard]] std::optional<Port> embedded() const;
    void set_session_port(std::optional<Port> port);
    // The session listener and the fixed listeners, which no upstream may be.
    [[nodiscard]] std::vector<Port> front_ports() const;

    // False while stopping, and the caller then closes the socket.
    [[nodiscard]] bool enter(const std::shared_ptr<FrontConnection>& connection, ListenerKind kind);
    void leave(const FrontConnection& connection);

    // Returns every live connection; `drained` runs on the I/O side once none is left.
    [[nodiscard]] std::vector<std::shared_ptr<FrontConnection>> begin_stop(UniqueFunction<void()> drained);
    [[nodiscard]] std::vector<std::shared_ptr<FrontConnection>> connections() const;
    void end_stop();

private:
    struct Entry {
        std::weak_ptr<FrontConnection> connection;
        ListenerKind kind = ListenerKind::Session;
        std::vector<SessionId> sessions;
    };

    void note_session(const FrontConnection& connection, SessionId session);

    mutable std::mutex mutex_;
    std::unordered_map<SessionId, std::shared_ptr<const RouteSnapshot>> routes_;
    std::unordered_map<SessionKey, SessionId> keys_;
    std::optional<SessionId> legacy_;
    std::vector<Port> legacy_ports_;
    std::optional<Port> embedded_;
    std::optional<Port> session_port_;
    std::unordered_map<const FrontConnection*, Entry> connections_;
    bool stopping_ = false;
    UniqueFunction<void()> drained_;
};

}  // namespace rb::front
