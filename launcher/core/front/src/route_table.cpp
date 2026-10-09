#include "route_table.hpp"

#include <algorithm>
#include <utility>

namespace reboot::front {

namespace {

[[nodiscard]] bool contains(const std::vector<UpstreamOrigin>& origins, const UpstreamOrigin& target) {
    return std::ranges::find(origins, target) != origins.end();
}

}  // namespace

RelayVerdict relay_verdict(const RouteSnapshot& route, const UpstreamOrigin& target) {
    if (!route.configured) return RelayVerdict::Forbidden;
    const ConfiguredView& view = *route.configured;
    if (!view.policy.allows(target)) return RelayVerdict::Forbidden;
    if (target.scheme == net::UrlScheme::Https || target == view.policy.backend()) return RelayVerdict::Allowed;
    if (view.plain_http_consented && target.host == view.policy.backend().host) return RelayVerdict::Allowed;
    if (contains(view.consented, target)) return RelayVerdict::Allowed;
    if (contains(view.declined, target)) return RelayVerdict::Forbidden;
    return RelayVerdict::AwaitingConsent;
}

std::optional<std::array<u8, 32>> pin_for(const RouteSnapshot& route, const UpstreamOrigin& target) {
    if (!route.configured || target.host != route.configured->policy.backend().host) return std::nullopt;
    return route.configured->pin;
}

void RouteTable::put(std::shared_ptr<const RouteSnapshot> route) {
    const std::scoped_lock lock(mutex_);
    keys_.insert_or_assign(route->key, route->session);
    routes_.insert_or_assign(route->session, std::move(route));
}

std::vector<std::shared_ptr<FrontConnection>> RouteTable::erase(SessionId session) {
    const std::scoped_lock lock(mutex_);
    const auto route = routes_.find(session);
    if (route != routes_.end()) {
        keys_.erase(route->second->key);
        routes_.erase(route);
    }
    std::vector<std::shared_ptr<FrontConnection>> out;
    for (const auto& [key, entry] : connections_) {
        if (std::ranges::find(entry.sessions, session) == entry.sessions.end()) continue;
        if (auto connection = entry.connection.lock()) out.push_back(std::move(connection));
    }
    return out;
}

void RouteTable::note_session(const FrontConnection& connection, SessionId session) {
    const auto entry = connections_.find(&connection);
    if (entry == connections_.end()) return;
    if (std::ranges::find(entry->second.sessions, session) == entry->second.sessions.end())
        entry->second.sessions.push_back(session);
}

std::shared_ptr<const RouteSnapshot> RouteTable::claim(const SessionKey& key, const FrontConnection& connection) {
    const std::scoped_lock lock(mutex_);
    const auto session = keys_.find(key);
    if (session == keys_.end()) return nullptr;
    note_session(connection, session->second);
    return routes_.at(session->second);
}

std::shared_ptr<const RouteSnapshot> RouteTable::claim_legacy(const FrontConnection& connection) {
    const std::scoped_lock lock(mutex_);
    if (!legacy_) return nullptr;
    const auto route = routes_.find(*legacy_);
    if (route == routes_.end()) return nullptr;
    note_session(connection, *legacy_);
    return route->second;
}

std::vector<std::shared_ptr<FrontConnection>> RouteTable::set_legacy(std::optional<SessionId> session,
                                                                     std::vector<Port> ports) {
    const std::scoped_lock lock(mutex_);
    legacy_ = session;
    legacy_ports_ = std::move(ports);
    std::vector<std::shared_ptr<FrontConnection>> out;
    if (session) return out;
    for (const auto& [key, entry] : connections_) {
        if (entry.kind != ListenerKind::Fixed) continue;
        if (auto connection = entry.connection.lock()) out.push_back(std::move(connection));
    }
    return out;
}

void RouteTable::set_embedded(std::optional<Port> port) {
    const std::scoped_lock lock(mutex_);
    embedded_ = port;
}

std::optional<Port> RouteTable::embedded() const {
    const std::scoped_lock lock(mutex_);
    return embedded_;
}

void RouteTable::set_session_port(std::optional<Port> port) {
    const std::scoped_lock lock(mutex_);
    session_port_ = port;
}

std::vector<Port> RouteTable::front_ports() const {
    const std::scoped_lock lock(mutex_);
    std::vector<Port> out = legacy_ports_;
    if (session_port_) out.push_back(*session_port_);
    return out;
}

bool RouteTable::enter(const std::shared_ptr<FrontConnection>& connection, ListenerKind kind) {
    const std::scoped_lock lock(mutex_);
    if (stopping_ || (kind == ListenerKind::Fixed && !legacy_)) return false;
    connections_.insert_or_assign(connection.get(), Entry{connection, kind, {}});
    return true;
}

void RouteTable::leave(const FrontConnection& connection) {
    UniqueFunction<void()> drained;
    {
        const std::scoped_lock lock(mutex_);
        connections_.erase(&connection);
        if (stopping_ && connections_.empty()) drained = std::move(drained_);
    }
    if (drained) drained();
}

std::vector<std::shared_ptr<FrontConnection>> RouteTable::begin_stop(UniqueFunction<void()> drained) {
    std::vector<std::shared_ptr<FrontConnection>> out;
    {
        const std::scoped_lock lock(mutex_);
        stopping_ = true;
        for (const auto& [key, entry] : connections_)
            if (auto connection = entry.connection.lock()) out.push_back(std::move(connection));
        if (!connections_.empty()) {
            drained_ = std::move(drained);
            return out;
        }
    }
    drained();
    return out;
}

std::vector<std::shared_ptr<FrontConnection>> RouteTable::connections() const {
    const std::scoped_lock lock(mutex_);
    std::vector<std::shared_ptr<FrontConnection>> out;
    for (const auto& [key, entry] : connections_)
        if (auto connection = entry.connection.lock()) out.push_back(std::move(connection));
    return out;
}

void RouteTable::end_stop() {
    const std::scoped_lock lock(mutex_);
    stopping_ = false;
    drained_ = nullptr;
}

}  // namespace reboot::front
