#pragma once

#include <chrono>
#include <cstddef>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/function.hpp"
#include "reboot/foundation/net_types.hpp"
#include "reboot/foundation/types.hpp"
#include "reboot/net/mapping_record.hpp"
#include "reboot/net/port_mapping.hpp"

namespace rb {
class EventBus;
class Executor;
class TimerService;
class WorkerPool;
}  // namespace rb

namespace rb::net {

class IPortMappingGateway;

inline constexpr std::chrono::seconds kMappingLease{3600};
// Entries with this prefix plus our engine tag are ours to sweep.
inline constexpr std::string_view kMappingDescriptionPrefix = "Reboot Launcher";
// The leading hex digits of the session id that a description carries.
inline constexpr std::size_t kMappingSessionDigits = 8;
// macOS can deny the first discovery at once while its local-network alert is showing.
inline constexpr u32 kGatewayDiscoveryAttempts = 3;
inline constexpr std::chrono::seconds kGatewayDiscoveryRetryDelay{2};

// "<prefix> <engine_tag>/<first kMappingSessionDigits of session>": 41 characters, which fits
// miniupnpd's 64-byte description buffer.
[[nodiscard]] std::string mapping_description(std::string_view engine_tag, const SessionId& session);

// Capabilities: none; implements hosting-port-forwarding and concurrent-hosts port mapping.
// Strand-only. Maps a session's whole UDP port block, UPnP first and NAT-PMP second, with
// kMappingLease renewed at half the lease. UPnP 725 retries with lease 0 and deletes on stop;
// 718 retries with another external port. Discovery is tried kGatewayDiscoveryAttempts times.
// Failure is only ever a PortMappingChanged diagnostic: hosting continues with the bound port.
// Gateway calls run on the WorkerPool within the UpnpDiscover and UpnpMap deadlines.
class PortMapperService {
public:
    // `engine_tag` is root_hash16, so engines on other data roots never sweep our entries.
    // `recorded` is the sweep marker loaded from state/state.json; `persist` receives the full
    // set after every change, and the engine writes it back there.
    PortMapperService(IPortMappingGateway& upnp, IPortMappingGateway& natpmp, std::string engine_tag,
                      std::vector<MappingRecord> recorded, UniqueFunction<void(std::vector<MappingRecord>)> persist,
                      WorkerPool& workers, Executor& strand, TimerService& timers, EventBus& events);
    ~PortMapperService();
    PortMapperService(const PortMapperService&) = delete;
    PortMapperService& operator=(const PortMapperService&) = delete;

    // `block` starts with the game port. Fails synchronously on an empty block or a session that
    // is already mapped. The outcome arrives as PortMappingChanged.
    Result<void> map(const SessionId& session, std::span<const Port> block);

    // Stops renewal and deletes the session's entries; `done` runs on the strand.
    void unmap(const SessionId& session, UniqueFunction<void()> done);
    // ShutdownCoordinator's step: unmaps every session.
    void unmap_all(UniqueFunction<void()> done);

    // At engine start, sparing `live_sessions`: deletes every recorded mapping, then the UPnP
    // entries pointing at our LAN address whose description starts with our prefix and engine
    // tag. Live sessions are matched on the description's session digits.
    void sweep_stale(std::span<const SessionId> live_sessions, UniqueFunction<void()> done);

    [[nodiscard]] std::vector<PortMapping> mappings(const SessionId& session) const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace rb::net
