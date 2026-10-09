#include "strand_routes.hpp"

#include <utility>
#include <variant>

#include "front_core.hpp"
#include "messages.hpp"
#include "reboot/foundation/log.hpp"
#include "reboot/foundation/user_request.hpp"
#include "reboot/front/unencrypted_upstream_prompt.hpp"
#include "reboot/net/host_tls_memory.hpp"

namespace rb::front {

namespace {

// HostTlsMemory's refusal that an answer can lift; a downgrade refusal never can.
constexpr std::string_view kNeedsConsent = "net.plain_http_needs_consent";

[[nodiscard]] std::string session_text(SessionId session) { return format_uuid(session.value); }

}  // namespace

StrandRoutes::StrandRoutes(RouteTable& table, net::HostTlsMemory& tls, UserRequestRegistry& requests)
    : table_(table), tls_(tls), requests_(requests) {}

Result<void> StrandRoutes::add(FrontRoute route) {
    if (routes_.contains(route.session))
        return make_diag(ErrorDomain::Front, msg::kRouteExists)
            .arg("session", session_text(route.session))
            .kind(ErrorKind::Conflict)
            .fail();
    for (const auto& [session, state] : routes_)
        if (state.route.key == route.key)
            return make_diag(ErrorDomain::Front, msg::kKeyInUse).kind(ErrorKind::Conflict).fail();

    std::optional<UpstreamPolicy> policy;
    if (const auto* configured = std::get_if<ConfiguredUpstream>(&route.upstream)) {
        const UpstreamOrigin& origin = configured->origin;
        if (origin.scheme == net::UrlScheme::Http) {
            Result<void> allowed = tls_.check(net::UrlScheme::Http, origin.host);
            if (!allowed && !(configured->plain_http_consented && allowed.error().id == kNeedsConsent))
                return std::unexpected(std::move(allowed.error()));
        }
        policy.emplace(origin);
    }
    const SessionId session = route.session;
    const auto [state, inserted] =
        routes_.emplace(session, RouteState{std::move(route), std::move(policy), {}, {}, CancelSource{}});
    publish(state->second);
    return {};
}

std::vector<std::shared_ptr<FrontConnection>> StrandRoutes::remove(SessionId session) {
    if (const auto state = routes_.find(session); state != routes_.end()) {
        CancelSource prompts = state->second.prompts;
        routes_.erase(state);
        prompts.cancel(CancelReason::Superseded);
    }
    return table_.erase(session);
}

bool StrandRoutes::contains(SessionId session) const { return routes_.contains(session); }

TicketSwap StrandRoutes::swap(SessionId session, std::span<const u8> form, PeerUser peer) {
    const auto state = routes_.find(session);
    if (state == routes_.end()) return {TicketSwapOutcome::Refused, {}};
    if (!state->second.route.tickets) return {};
    return state->second.route.tickets->swap(form, peer);
}

void StrandRoutes::settle(SessionId session, std::optional<u32> upstream_status) {
    const auto state = routes_.find(session);
    if (state != routes_.end() && state->second.route.tickets) state->second.route.tickets->settle(upstream_status);
}

void StrandRoutes::learn(SessionId session, std::string_view path, std::span<const u8> body) {
    const auto state = routes_.find(session);
    if (state == routes_.end() || !state->second.policy) return;
    const std::vector<UpstreamOrigin> added = state->second.policy->learn(path, body);
    if (added.empty()) return;
    for (const UpstreamOrigin& origin : added) {
        REBOOT_LOG_AT(LogLevel::Debug, Net, session, "front: learned upstream {}", origin.to_string());
        if (origin.scheme == net::UrlScheme::Http) consider(state->second, origin);
    }
    publish(state->second);
}

void StrandRoutes::remember_https(std::string_view host) { tls_.remember_https(host); }

void StrandRoutes::publish(const RouteState& state) {
    auto snapshot = std::make_shared<RouteSnapshot>();
    snapshot->session = state.route.session;
    snapshot->key = state.route.key;
    snapshot->tickets = state.route.tickets.has_value();
    if (state.policy) {
        const auto& configured = std::get<ConfiguredUpstream>(state.route.upstream);
        snapshot->configured = ConfiguredView{*state.policy, configured.pinned_certificate,
                                              configured.plain_http_consented, state.consented, state.declined};
    }
    table_.put(std::move(snapshot));
}

void StrandRoutes::consider(RouteState& state, const UpstreamOrigin& origin) {
    const auto& configured = std::get<ConfiguredUpstream>(state.route.upstream);
    // The lease's consent covers the backend's host; relay_verdict allows it as is.
    if (configured.plain_http_consented && origin.host == configured.origin.host) return;
    const Result<void> allowed = tls_.check(net::UrlScheme::Http, origin.host);
    if (allowed) {
        state.consented.push_back(origin);
        return;
    }
    if (allowed.error().id != kNeedsConsent) {
        state.declined.push_back(origin);
        return;
    }
    const SessionId session = state.route.session;
    static_cast<void>(requests_.ask(
        UserRequestKind::ConfirmUnencryptedUpstream, UnencryptedUpstreamPrompt{origin.to_string()}, std::nullopt,
        session,
        [weak = weak_from_this(), session, origin](const std::any& answer) -> Result<void> {
            const std::shared_ptr<StrandRoutes> self = weak.lock();
            if (!self) return {};
            return self->answered(session, origin, answer);
        },
        state.prompts.token()));
}

Result<void> StrandRoutes::answered(SessionId session, const UpstreamOrigin& origin, const std::any& answer) {
    const auto* reply = std::any_cast<UnencryptedUpstreamAnswer>(&answer);
    if (reply == nullptr)
        return make_diag(ErrorDomain::Front, msg::kUnexpectedAnswer)
            .arg("origin", origin.to_string())
            .kind(ErrorKind::InvalidInput)
            .fail();
    const auto state = routes_.find(session);
    if (state == routes_.end()) return {};
    if (reply->accept) {
        state->second.consented.push_back(origin);
        if (reply->remember) tls_.remember_http_acknowledged(origin.host);
    } else {
        state->second.declined.push_back(origin);
    }
    publish(state->second);
    return {};
}

}  // namespace rb::front
