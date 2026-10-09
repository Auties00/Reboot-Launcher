#include "reboot/browser/game_server_target.hpp"

#include <string>
#include <utility>

#include "messages.hpp"
#include "reboot/foundation/events.hpp"
#include "reboot/net/address_resolver.hpp"
#include "reboot/net/resolve_error.hpp"
#include "reboot/net/udp_beacon_prober.hpp"
#include "text_util.hpp"

namespace reboot::browser {

namespace {

constexpr std::string_view kNoIpv4Id = "net.no_ipv4_address";

[[nodiscard]] std::string address_text(const HostPort& address) {
    const bool ipv6_literal = address.host.find(':') != std::string::npos;
    std::string text = ipv6_literal ? '[' + address.host + ']' : address.host;
    if (address.port) text += ':' + std::to_string(address.port->value);
    return text;
}

}  // namespace

bool is_local_host(const Endpoint& endpoint) noexcept {
    if (endpoint.address.is_loopback()) return true;
    const auto& bytes = endpoint.address.bytes;
    return endpoint.address.is_v4() && bytes[12] == 0 && bytes[13] == 0 && bytes[14] == 0 && bytes[15] == 0;
}

Result<HostPort> parse_game_server_address(std::string_view text) {
    const std::string_view trimmed = trim_ascii(text);
    auto parsed = parse_host_port(trimmed);
    if (!parsed)
        return std::unexpected(net::to_diagnostic(net::ResolveError{
            .code = net::ResolveErrorCode::InvalidAddress, .address = std::string(trimmed), .parse_error = parsed.error()}));
    if (!parsed->port) parsed->port = net::kDefaultGamePort;
    return std::move(*parsed);
}

GameServerTarget::GameServerTarget(net::AddressResolver& resolver, net::UdpBeaconProber& prober, OpRegistry& ops,
                                   EventBus& events, std::optional<JoinTarget> loaded, Persist persist)
    : resolver_(resolver), prober_(prober), ops_(ops), events_(events), persist_(std::move(persist)),
      current_(std::move(loaded)), self_(std::make_shared<GameServerTarget*>(this)) {}

GameServerTarget::~GameServerTarget() {
    *self_ = nullptr;
    supersede_pending();
}

Result<OpHandle> GameServerTarget::start_set_custom(std::string_view text, DisconnectPolicy policy) {
    Result<HostPort> address = parse_game_server_address(text);
    if (!address) return std::unexpected(std::move(address.error()));
    supersede_pending();

    auto [handle, op] = ops_.create<CheckedAddress>(OpKind::Generic, policy, std::nullopt, RunnerMultiplier::Native,
                                                    kCheckAddressDeadline);
    const OpId id = handle.id();
    pending_set_ = id;
    op.progress(Progress{.phase = "resolving"});

    AddressTarget target{std::string(trim_ascii(text)), *address};
    Operation<CheckedAddress>* operation = &op;
    auto adopt_resolved = [self = self_, id, operation, target = std::move(target)](const Endpoint&) mutable {
        GameServerTarget* owner = *self;
        // A newer set_*/clear call already won; this lookup must not land after it.
        if (owner == nullptr || owner->pending_set_ != id || operation->token().cancelled()) return;
        owner->adopt(JoinTarget{std::move(target)});
        operation->progress(Progress{.phase = "probing"});
    };
    auto finish = [self = self_, id, operation](Result<CheckedAddress> result) {
        if (GameServerTarget* owner = *self; owner != nullptr && owner->pending_set_ == id) owner->pending_set_.reset();
        if (result) {
            operation->complete(Completed<CheckedAddress>{std::move(*result)});
        } else if (const auto reason = operation->token().reason()) {
            operation->complete(Cancelled{*reason});
        } else {
            operation->complete(Failed{std::move(result.error())});
        }
    };
    if (Result<void> started = check(*address, op.token(), std::move(adopt_resolved), std::move(finish)); !started) {
        pending_set_.reset();
        op.complete(Failed{started.error()});
        return std::unexpected(std::move(started.error()));
    }
    return handle;
}

void GameServerTarget::set_server(ServerTarget server) {
    supersede_pending();
    adopt(JoinTarget{std::move(server)});
}

void GameServerTarget::clear() {
    supersede_pending();
    adopt(std::nullopt);
}

Result<void> GameServerTarget::resolve(const HostPort& address, OperationBase& op,
                                       UniqueFunction<void(Result<CheckedAddress>)> done) {
    return check(address, op.token(), [](const Endpoint&) {}, std::move(done));
}

void GameServerTarget::supersede_pending() {
    if (!pending_set_) return;
    const OpId id = *pending_set_;
    pending_set_.reset();
    (void)ops_.cancel(id, CancelReason::Superseded);
}

void GameServerTarget::adopt(std::optional<JoinTarget> target) {
    if (target == current_) return;
    current_ = std::move(target);
    if (persist_) persist_(current_);
    events_.publish(EventKind::JoinTargetChanged, JoinTargetChanged{current_});
}

Result<void> GameServerTarget::check(const HostPort& address, CancelToken token,
                                     UniqueFunction<void(const Endpoint&)> resolved,
                                     UniqueFunction<void(Result<CheckedAddress>)> done) {
    const std::string text = address_text(address);
    auto finish = std::make_shared<UniqueFunction<void(Result<CheckedAddress>)>>(std::move(done));
    const auto cancelled = [](std::string address_shown) {
        return make_diag(ErrorDomain::Browser, kAddressCheckCancelled)
            .arg("address", std::move(address_shown))
            .kind(ErrorKind::Cancelled)
            .fail();
    };
    return resolver_.resolve(
        text, net::kDefaultGamePort, net::AddressFamilyPolicy::Ipv4Only, token,
        [self = self_, text, token, resolved = std::move(resolved), finish, cancelled](
            Result<net::ResolvedAddress> lookup) mutable {
            if (!lookup) {
                if (lookup.error().id == kNoIpv4Id)
                    (*finish)(make_diag(ErrorDomain::Browser, kUnsupportedAddressFamily)
                                  .arg("address", text)
                                  .kind(ErrorKind::Unsupported)
                                  .cause(std::move(lookup.error()))
                                  .fail());
                else
                    (*finish)(std::unexpected(std::move(lookup.error())));
                return;
            }
            const Endpoint endpoint = lookup->endpoints.front();
            GameServerTarget* owner = *self;
            if (owner == nullptr) {
                (*finish)(cancelled(text));
                return;
            }
            resolved(endpoint);
            if (is_local_host(endpoint)) {
                (*finish)(CheckedAddress{endpoint, ProbeVerdict::Skipped, std::nullopt});
                return;
            }
            Result<void> started = owner->prober_.probe(
                endpoint, net::ProbePolicy{}, token, [endpoint, finish, cancelled](net::ProbeResult probe) {
                    if (probe.outcome == net::ProbeOutcome::Cancelled) {
                        (*finish)(cancelled(endpoint.to_string()));
                        return;
                    }
                    CheckedAddress checked{endpoint, ProbeVerdict::Reachable, std::nullopt};
                    if (probe.outcome != net::ProbeOutcome::Alive) {
                        checked.probe = ProbeVerdict::Unreachable;
                        checked.warning = make_diag(ErrorDomain::Browser, kTargetUnreachable)
                                              .arg("address", endpoint.to_string())
                                              .severity(Severity::Warning)
                                              .build();
                    }
                    (*finish)(std::move(checked));
                });
            if (!started) (*finish)(std::unexpected(std::move(started.error())));
        });
}

}  // namespace reboot::browser
