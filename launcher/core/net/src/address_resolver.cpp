#include "reboot/net/address_resolver.hpp"

#include <algorithm>
#include <map>
#include <optional>
#include <string>
#include <utility>

#include "reboot/foundation/executor.hpp"
#include "reboot/foundation/operation.hpp"
#include "reboot/net/resolve_error.hpp"
#include "reboot/ports/net.hpp"
#include "url.hpp"

namespace rb::net {

namespace {

// Names the game treats as this machine; a connected UDP socket to 0.0.0.0 fails on Windows.
[[nodiscard]] bool is_local_alias(std::string_view host) noexcept {
    return host == "localhost" || host == "0.0.0.0" || host == "127.0.0.1";
}

[[nodiscard]] Diagnostic resolve_failure(ResolveErrorCode code, const std::string& address,
                                         std::optional<Diagnostic> cause = std::nullopt) {
    return to_diagnostic(ResolveError{.code = code, .address = address, .cause = std::move(cause)});
}

[[nodiscard]] Result<ResolvedAddress> to_resolved(HostPort parsed, Port port, std::span<const IpAddress> addresses,
                                                  AddressFamilyPolicy family, const std::string& address) {
    ResolvedAddress out;
    for (const IpAddress& ip : addresses) {
        if (family == AddressFamilyPolicy::Ipv4Only && !ip.is_v4()) continue;
        const Endpoint endpoint{ip, port};
        if (std::ranges::find(out.endpoints, endpoint) == out.endpoints.end()) out.endpoints.push_back(endpoint);
    }
    if (out.endpoints.empty()) {
        const bool filtered = family == AddressFamilyPolicy::Ipv4Only && !addresses.empty();
        return std::unexpected(resolve_failure(filtered ? ResolveErrorCode::NoIpv4Address : ResolveErrorCode::NotFound, address));
    }
    out.parsed = std::move(parsed);
    return out;
}

}  // namespace

struct AddressResolver::Impl {
    struct Lookup {
        std::string address;
        HostPort parsed;
        Port port;
        AddressFamilyPolicy family = AddressFamilyPolicy::Any;
        CancelSource resolver_cancel;
        CancelRegistration user_cancel;
        TimerHandle deadline;
        UniqueFunction<void(Result<ResolvedAddress>)> done;
    };

    struct Core : std::enable_shared_from_this<Core> {
        Core(ports::IResolver& resolver_in, Executor& strand_in, TimerService& timers_in)
            : resolver(resolver_in), strand(strand_in), timers(timers_in) {}

        void finish(u64 id, Result<ResolvedAddress> result) {
            const auto it = lookups.find(id);
            if (it == lookups.end()) return;
            Lookup lookup = std::move(it->second);
            lookups.erase(it);
            lookup.deadline.cancel();
            lookup.user_cancel.reset();
            lookup.resolver_cancel.cancel(CancelReason::Superseded);
            lookup.done(std::move(result));
        }

        void answered(u64 id, Result<std::vector<IpAddress>> answer) {
            const auto it = lookups.find(id);
            if (it == lookups.end()) return;
            const Lookup& lookup = it->second;
            if (!answer) {
                const bool not_found = answer.error().kind == ErrorKind::NotFound;
                finish(id, std::unexpected(resolve_failure(not_found ? ResolveErrorCode::NotFound : ResolveErrorCode::Failed,
                                                           lookup.address, std::move(answer.error()))));
                return;
            }
            finish(id, to_resolved(lookup.parsed, lookup.port, *answer, lookup.family, lookup.address));
        }

        ports::IResolver& resolver;
        Executor& strand;
        TimerService& timers;
        u64 next_id = 1;
        std::map<u64, Lookup> lookups;
    };

    Impl(ports::IResolver& resolver, Executor& strand, TimerService& timers)
        : core(std::make_shared<Core>(resolver, strand, timers)) {}

    ~Impl() {
        for (auto& [id, lookup] : core->lookups) {
            lookup.user_cancel.reset();
            lookup.resolver_cancel.cancel(CancelReason::Shutdown);
        }
        core->lookups.clear();
    }

    std::shared_ptr<Core> core;
};

AddressResolver::AddressResolver(ports::IResolver& resolver, Executor& strand, TimerService& timers)
    : impl_(std::make_unique<Impl>(resolver, strand, timers)) {}

AddressResolver::~AddressResolver() = default;

Result<void> AddressResolver::resolve(std::string_view text, Port default_port, AddressFamilyPolicy family,
                                      CancelToken token, UniqueFunction<void(Result<ResolvedAddress>)> done) {
    std::expected<HostPort, AddressError> parsed = parse_host_port(text);
    if (!parsed)
        return std::unexpected(to_diagnostic(
            ResolveError{.code = ResolveErrorCode::InvalidAddress, .address = std::string(text), .parse_error = parsed.error()}));

    Impl::Core& core = *impl_->core;
    const u64 id = core.next_id++;
    Impl::Lookup& lookup = core.lookups[id];
    lookup.address = std::string(text);
    lookup.port = parsed->port.value_or(default_port);
    lookup.family = family;
    lookup.done = std::move(done);
    const std::string host = normalize_host(parsed->host);
    lookup.parsed = std::move(*parsed);

    const std::weak_ptr<Impl::Core> weak = core.weak_from_this();
    lookup.user_cancel = token.on_cancel([weak, id](CancelReason) {
        if (const std::shared_ptr<Impl::Core> self = weak.lock())
            self->strand.post([weak, id] {
                if (const std::shared_ptr<Impl::Core> owner = weak.lock()) {
                    const auto it = owner->lookups.find(id);
                    if (it != owner->lookups.end())
                        owner->finish(id, std::unexpected(resolve_failure(ResolveErrorCode::Cancelled, it->second.address)));
                }
            });
    });

    std::optional<IpAddress> literal = is_local_alias(host) ? IpAddress::parse("127.0.0.1") : IpAddress::parse(host);
    if (literal) {
        core.strand.post([weak, id, ip = *literal] {
            const std::shared_ptr<Impl::Core> self = weak.lock();
            if (!self) return;
            const auto it = self->lookups.find(id);
            if (it == self->lookups.end()) return;
            const Impl::Lookup& pending = it->second;
            self->finish(id, to_resolved(pending.parsed, pending.port, std::span(&ip, 1), pending.family, pending.address));
        });
        return {};
    }

    lookup.deadline = core.timers.after(default_deadline(OpKind::Dns), [weak, id] {
        if (const std::shared_ptr<Impl::Core> self = weak.lock()) {
            const auto it = self->lookups.find(id);
            if (it != self->lookups.end())
                self->finish(id, std::unexpected(resolve_failure(ResolveErrorCode::Timeout, it->second.address)));
        }
    });
    core.resolver.resolve(host, lookup.resolver_cancel.token(), [weak, id](Result<std::vector<IpAddress>> answer) mutable {
        const std::shared_ptr<Impl::Core> self = weak.lock();
        if (!self) return;
        self->strand.post([weak, id, answer = std::move(answer)]() mutable {
            if (const std::shared_ptr<Impl::Core> owner = weak.lock()) owner->answered(id, std::move(answer));
        });
    });
    return {};
}

}  // namespace rb::net
