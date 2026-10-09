#include "reboot/net/port_mapper_service.hpp"

#include <algorithm>
#include <format>
#include <map>
#include <optional>
#include <set>
#include <utility>

#include "gateway_diagnostic.hpp"
#include "messages.hpp"
#include "reboot/foundation/events.hpp"
#include "reboot/foundation/executor.hpp"
#include "reboot/foundation/operation.hpp"
#include "reboot/net/port_mapping_gateway.hpp"

namespace reboot::net {

namespace {

// How many other external ports a 718 conflict tries before the port counts as refused.
constexpr u32 kExternalPortTries = 8;

[[nodiscard]] std::string session_digits(const SessionId& session) {
    return format_uuid(session.value).substr(0, kMappingSessionDigits);
}

struct Discovery {
    MappingMethod method = MappingMethod::Upnp;
    GatewayInfo info;
};

struct PortOutcome {
    Port internal;
    std::expected<PortMapping, GatewayError> granted;
};

struct MapJobResult {
    std::vector<PortOutcome> ports;
};

[[nodiscard]] Port next_external(Port port, u32 step) {
    const u32 span = 65535 - 1024 + 1;
    const u32 value = 1024 + (static_cast<u32>(port.value) - 1024 + step) % span;
    return Port{static_cast<u16>(value)};
}

}  // namespace

std::string mapping_description(std::string_view engine_tag, const SessionId& session) {
    return std::format("{} {}/{}", kMappingDescriptionPrefix, engine_tag, session_digits(session));
}

struct PortMapperService::Impl {
    enum class Phase : u8 { Discovering, Mapping, Mapped, Renewing, Unmapping };

    struct Session {
        SessionId id;
        std::vector<Port> block;
        Phase phase = Phase::Discovering;
        std::optional<Discovery> gateway;
        std::vector<PortMapping> mappings;
        CancelSource cancel;
        TimerHandle timer;
        u32 discovery_attempts = 0;
        bool unmap_requested = false;
        std::vector<UniqueFunction<void()>> unmap_waiters;
    };

    struct Core : std::enable_shared_from_this<Core> {
        Core(IPortMappingGateway& upnp_in, IPortMappingGateway& natpmp_in, std::string engine_tag_in,
             std::vector<MappingRecord> recorded, UniqueFunction<void(std::vector<MappingRecord>)> persist_in,
             WorkerPool& workers_in, Executor& strand_in, TimerService& timers_in, EventBus& events_in)
            : upnp(upnp_in),
              natpmp(natpmp_in),
              engine_tag(std::move(engine_tag_in)),
              records(std::move(recorded)),
              persist(std::move(persist_in)),
              workers(workers_in),
              strand(strand_in),
              timers(timers_in),
              events(events_in) {}

        IPortMappingGateway& gateway_for(MappingMethod method) { return method == MappingMethod::NatPmp ? natpmp : upnp; }

        // Runs `work` on a worker and `then` on the strand while the service lives.
        template <class T, class Work, class Then>
        void run(CancelToken token, Work&& work, Then&& then) {
            workers.submit<T>(
                UniqueFunction<Result<T>(CancelToken)>(std::forward<Work>(work)), std::move(token), strand,
                [weak = weak_from_this(), then = std::forward<Then>(then)](Result<T> result) mutable {
                    if (const std::shared_ptr<Core> self = weak.lock()) then(*self, std::move(result));
                });
        }

        void discover(const SessionId& id) {
            Session& session = sessions.at(id);
            session.phase = Phase::Discovering;
            ++session.discovery_attempts;
            run<Discovery>(
                session.cancel.token(),
                [&upnp = upnp, &natpmp = natpmp](CancelToken token) -> Result<Discovery> {
                    const auto timeout = default_deadline(OpKind::UpnpDiscover);
                    std::expected<GatewayInfo, GatewayError> found = upnp.discover(timeout, token);
                    if (found) return Discovery{MappingMethod::Upnp, std::move(*found)};
                    if (token.cancelled()) return std::unexpected(gateway_diagnostic(GatewayError{GatewayErrorCode::NoGateway}));
                    found = natpmp.discover(timeout, token);
                    if (found) return Discovery{MappingMethod::NatPmp, std::move(*found)};
                    return std::unexpected(gateway_diagnostic(GatewayError{GatewayErrorCode::NoGateway}));
                },
                [id](Core& self, Result<Discovery> result) { self.discovered(id, std::move(result)); });
        }

        void discovered(const SessionId& id, Result<Discovery> result) {
            const auto it = sessions.find(id);
            if (it == sessions.end()) return;
            Session& session = it->second;
            if (session.unmap_requested) {
                remove_all(id);
                return;
            }
            if (!result) {
                if (session.discovery_attempts < kGatewayDiscoveryAttempts) {
                    session.timer = timers.after(kGatewayDiscoveryRetryDelay, [weak = weak_from_this(), id] {
                        if (const std::shared_ptr<Core> self = weak.lock(); self && self->sessions.contains(id)) self->discover(id);
                    });
                    return;
                }
                session.phase = Phase::Mapped;
                publish(session, std::move(result.error()));
                return;
            }
            session.gateway = std::move(*result);
            add_ports(id, session.block, Phase::Mapping);
        }

        // Maps `ports`, each first at its own number (or its current external port when renewing).
        void add_ports(const SessionId& id, std::vector<Port> ports, Phase phase) {
            Session& session = sessions.at(id);
            session.phase = phase;
            std::vector<GatewayMappingRequest> requests;
            for (const Port port : ports) {
                GatewayMappingRequest request;
                request.internal = port;
                request.external = port;
                for (const PortMapping& mapping : session.mappings)
                    if (mapping.internal == port) request.external = mapping.external;
                request.lan_address = session.gateway->info.lan_address;
                request.lease = kMappingLease;
                request.description = mapping_description(engine_tag, id);
                requests.push_back(std::move(request));
            }
            std::set<Port> taken;
            for (const auto& [other_id, other] : sessions)
                if (other_id != id)
                    for (const PortMapping& mapping : other.mappings) taken.insert(mapping.external);

            IPortMappingGateway& gateway = gateway_for(session.gateway->method);
            run<MapJobResult>(
                session.cancel.token(),
                [&gateway, requests = std::move(requests), taken = std::move(taken)](CancelToken token) mutable -> Result<MapJobResult> {
                    MapJobResult out;
                    for (GatewayMappingRequest request : requests) {
                        if (token.cancelled()) break;
                        out.ports.push_back(PortOutcome{request.internal, add_one(gateway, request, taken)});
                        if (out.ports.back().granted) taken.insert(out.ports.back().granted->external);
                    }
                    return out;
                },
                [id](Core& self, Result<MapJobResult> result) { self.added(id, std::move(result)); });
        }

        static std::expected<PortMapping, GatewayError> add_one(IPortMappingGateway& gateway, GatewayMappingRequest request,
                                                                const std::set<Port>& taken) {
            const auto timeout = default_deadline(OpKind::UpnpMap);
            const Port first = request.external;
            u32 step = 0;
            while (taken.contains(request.external) && step < kExternalPortTries) request.external = next_external(first, ++step);
            bool permanent_tried = false;
            while (true) {
                std::expected<PortMapping, GatewayError> granted = gateway.add(request, timeout);
                if (granted) return granted;
                if (granted.error().code == GatewayErrorCode::OnlyPermanentLease && !permanent_tried) {
                    permanent_tried = true;
                    request.lease = std::chrono::seconds{0};
                    continue;
                }
                if (granted.error().code == GatewayErrorCode::ExternalPortTaken && step < kExternalPortTries) {
                    do {
                        request.external = next_external(first, ++step);
                    } while (taken.contains(request.external) && step < kExternalPortTries);
                    continue;
                }
                return granted;
            }
        }

        void added(const SessionId& id, Result<MapJobResult> result) {
            const auto it = sessions.find(id);
            if (it == sessions.end()) return;
            Session& session = it->second;
            const bool renewing = session.phase == Phase::Renewing;
            std::optional<Diagnostic> failure;
            bool changed = !renewing;
            if (!result) {
                failure = std::move(result.error());
            } else {
                for (PortOutcome& outcome : result->ports) {
                    auto existing = std::ranges::find(session.mappings, outcome.internal, &PortMapping::internal);
                    if (outcome.granted) {
                        if (existing == session.mappings.end()) {
                            session.mappings.push_back(*outcome.granted);
                            changed = true;
                        } else {
                            if (existing->external != outcome.granted->external) changed = true;
                            *existing = *outcome.granted;
                        }
                        continue;
                    }
                    if (existing != session.mappings.end()) {
                        session.mappings.erase(existing);
                        changed = true;
                    }
                    if (!failure)
                        failure = mapping_failure(renewing ? kMappingRenewFailed : kMappingRefused, outcome.internal,
                                                  outcome.granted.error());
                }
            }
            if (failure) changed = true;
            store_records();
            if (session.unmap_requested) {
                remove_all(id);
                return;
            }
            session.phase = Phase::Mapped;
            if (changed) publish(session, std::move(failure));
            schedule_renewal(session);
        }

        void schedule_renewal(Session& session) {
            std::optional<std::chrono::seconds> shortest;
            for (const PortMapping& mapping : session.mappings)
                if (mapping.lease > std::chrono::seconds::zero() && (!shortest || mapping.lease < *shortest)) shortest = mapping.lease;
            session.timer.cancel();
            if (!shortest) return;
            const auto delay = std::max<std::chrono::seconds>(*shortest / 2, std::chrono::seconds{1});
            session.timer = timers.after(delay, [weak = weak_from_this(), id = session.id] {
                const std::shared_ptr<Core> self = weak.lock();
                if (!self) return;
                const auto it = self->sessions.find(id);
                if (it == self->sessions.end() || it->second.phase != Phase::Mapped) return;
                std::vector<Port> ports;
                for (const PortMapping& mapping : it->second.mappings)
                    if (mapping.lease > std::chrono::seconds::zero()) ports.push_back(mapping.internal);
                self->add_ports(id, std::move(ports), Phase::Renewing);
            });
        }

        void remove_all(const SessionId& id) {
            Session& session = sessions.at(id);
            session.phase = Phase::Unmapping;
            session.timer.cancel();
            std::vector<std::pair<MappingMethod, PortMapping>> doomed;
            for (const PortMapping& mapping : session.mappings) doomed.emplace_back(mapping.method, mapping);
            run<bool>(
                CancelToken{},
                [&upnp = upnp, &natpmp = natpmp, doomed = std::move(doomed)](CancelToken) -> Result<bool> {
                    for (const auto& [method, mapping] : doomed)
                        (void)(method == MappingMethod::NatPmp ? natpmp : upnp).remove(mapping, default_deadline(OpKind::UpnpMap));
                    return true;
                },
                [id](Core& self, Result<bool>) { self.removed(id); });
        }

        void removed(const SessionId& id) {
            const auto it = sessions.find(id);
            if (it == sessions.end()) return;
            // Recorded while the session is still known, so its records go rather than turn stale.
            it->second.mappings.clear();
            store_records();
            Session session = std::move(it->second);
            sessions.erase(it);
            publish(session, std::nullopt);
            for (UniqueFunction<void()>& waiter : session.unmap_waiters)
                if (waiter) waiter();
        }

        void store_records() {
            std::vector<MappingRecord> next;
            for (const MappingRecord& record : records)
                if (!sessions.contains(record.session)) next.push_back(record);
            for (const auto& [id, session] : sessions)
                for (const PortMapping& mapping : session.mappings) next.push_back(MappingRecord{id, mapping});
            const bool same = next.size() == records.size() &&
                              std::ranges::equal(next, records, [](const MappingRecord& a, const MappingRecord& b) {
                                  return a.session == b.session && a.mapping.internal == b.mapping.internal &&
                                         a.mapping.external == b.mapping.external && a.mapping.method == b.mapping.method &&
                                         a.mapping.lease == b.mapping.lease && a.mapping.lan_address == b.mapping.lan_address;
                              });
            records = std::move(next);
            if (!same && persist) persist(records);
        }

        void publish(const Session& session, std::optional<Diagnostic> failure) {
            PortMappingChanged changed;
            changed.session = session.id;
            changed.game_port = session.block.front();
            changed.mappings = session.mappings;
            changed.failure = std::move(failure);
            events.publish(EventKind::PortMappingChanged, std::move(changed),
                           EventScope{.session = session.id, .coalesce_key = format_uuid(session.id.value)});
        }

        IPortMappingGateway& upnp;
        IPortMappingGateway& natpmp;
        std::string engine_tag;
        std::vector<MappingRecord> records;
        UniqueFunction<void(std::vector<MappingRecord>)> persist;
        WorkerPool& workers;
        Executor& strand;
        TimerService& timers;
        EventBus& events;
        std::map<SessionId, Session> sessions;
    };

    Impl(IPortMappingGateway& upnp, IPortMappingGateway& natpmp, std::string engine_tag, std::vector<MappingRecord> recorded,
         UniqueFunction<void(std::vector<MappingRecord>)> persist, WorkerPool& workers, Executor& strand,
         TimerService& timers, EventBus& events)
        : core(std::make_shared<Core>(upnp, natpmp, std::move(engine_tag), std::move(recorded), std::move(persist),
                                      workers, strand, timers, events)) {}

    ~Impl() {
        for (auto& [id, session] : core->sessions) session.cancel.cancel(CancelReason::Shutdown);
    }

    std::shared_ptr<Core> core;
};

PortMapperService::PortMapperService(IPortMappingGateway& upnp, IPortMappingGateway& natpmp, std::string engine_tag,
                                     std::vector<MappingRecord> recorded,
                                     UniqueFunction<void(std::vector<MappingRecord>)> persist, WorkerPool& workers,
                                     Executor& strand, TimerService& timers, EventBus& events)
    : impl_(std::make_unique<Impl>(upnp, natpmp, std::move(engine_tag), std::move(recorded), std::move(persist), workers,
                                   strand, timers, events)) {}

PortMapperService::~PortMapperService() = default;

Result<void> PortMapperService::map(const SessionId& session, std::span<const Port> block) {
    Impl::Core& core = *impl_->core;
    if (block.empty() || core.sessions.contains(session))
        return make_diag(ErrorDomain::Net, kMappingBlockInvalid).kind(ErrorKind::InvalidInput).fail();
    Impl::Session& state = core.sessions[session];
    state.id = session;
    state.block.assign(block.begin(), block.end());
    core.discover(session);
    return {};
}

void PortMapperService::unmap(const SessionId& session, UniqueFunction<void()> done) {
    Impl::Core& core = *impl_->core;
    const auto it = core.sessions.find(session);
    if (it == core.sessions.end()) {
        if (done) core.strand.post(std::move(done));
        return;
    }
    Impl::Session& state = it->second;
    state.unmap_waiters.push_back(std::move(done));
    if (state.unmap_requested) return;
    state.unmap_requested = true;
    state.cancel.cancel(CancelReason::User);
    // A running discovery or mapping job removes what it granted when it returns.
    if (state.phase == Impl::Phase::Mapped) core.remove_all(session);
    else if (state.phase == Impl::Phase::Discovering && state.timer.active()) core.remove_all(session);
}

void PortMapperService::unmap_all(UniqueFunction<void()> done) {
    Impl::Core& core = *impl_->core;
    std::vector<SessionId> ids;
    for (const auto& [id, session] : core.sessions) ids.push_back(id);
    if (ids.empty()) {
        if (done) core.strand.post(std::move(done));
        return;
    }
    struct Countdown {
        std::size_t left = 0;
        UniqueFunction<void()> done;
    };
    auto countdown = std::make_shared<Countdown>(Countdown{ids.size(), std::move(done)});
    for (const SessionId& id : ids)
        unmap(id, [countdown] {
            if (--countdown->left == 0 && countdown->done) countdown->done();
        });
}

void PortMapperService::sweep_stale(std::span<const SessionId> live_sessions, UniqueFunction<void()> done) {
    Impl::Core& core = *impl_->core;
    std::vector<MappingRecord> stale;
    for (const MappingRecord& record : core.records)
        if (std::ranges::find(live_sessions, record.session) == live_sessions.end()) stale.push_back(record);
    std::set<std::string> live_digits;
    for (const SessionId& id : live_sessions) live_digits.insert(session_digits(id));
    const std::string ours = std::format("{} {}/", kMappingDescriptionPrefix, core.engine_tag);

    core.run<bool>(
        CancelToken{},
        [&upnp = core.upnp, &natpmp = core.natpmp, stale = std::move(stale), live_digits = std::move(live_digits),
         ours](CancelToken token) -> Result<bool> {
            const auto map_timeout = default_deadline(OpKind::UpnpMap);
            for (const MappingRecord& record : stale)
                (void)(record.mapping.method == MappingMethod::NatPmp ? natpmp : upnp).remove(record.mapping, map_timeout);
            const std::expected<GatewayInfo, GatewayError> gateway = upnp.discover(default_deadline(OpKind::UpnpDiscover), token);
            if (!gateway) return true;
            const std::expected<std::vector<GatewayEntry>, GatewayError> entries = upnp.list(map_timeout);
            if (!entries) return true;
            for (const GatewayEntry& entry : *entries) {
                if (entry.lan_address != gateway->lan_address || !entry.description.starts_with(ours)) continue;
                if (live_digits.contains(entry.description.substr(ours.size()))) continue;
                (void)upnp.remove(PortMapping{entry.internal, entry.external, MappingMethod::Upnp, std::chrono::seconds{0},
                                              entry.lan_address},
                                  map_timeout);
            }
            return true;
        },
        [live = std::vector<SessionId>(live_sessions.begin(), live_sessions.end()),
         done = std::move(done)](Impl::Core& self, Result<bool>) mutable {
            std::erase_if(self.records, [&](const MappingRecord& record) {
                return std::ranges::find(live, record.session) == live.end() && !self.sessions.contains(record.session);
            });
            if (self.persist) self.persist(self.records);
            if (done) done();
        });
}

std::vector<PortMapping> PortMapperService::mappings(const SessionId& session) const {
    const auto it = impl_->core->sessions.find(session);
    if (it == impl_->core->sessions.end()) return {};
    return it->second.mappings;
}

}  // namespace reboot::net
