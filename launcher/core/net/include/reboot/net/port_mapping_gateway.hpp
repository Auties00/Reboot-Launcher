#pragma once

#include <chrono>
#include <expected>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "reboot/foundation/cancel.hpp"
#include "reboot/foundation/net_types.hpp"
#include "reboot/net/gateway_error.hpp"
#include "reboot/net/port_mapping.hpp"

namespace rb::net {

struct GatewayInfo {
    IpAddress lan_address;
    std::optional<IpAddress> external_address;
};

struct GatewayMappingRequest {
    Port internal;
    Port external;
    IpAddress lan_address;
    std::chrono::seconds lease{0};
    std::string description;
};

// NAT-PMP entries carry no description, so only UPnP entries can be listed.
struct GatewayEntry {
    Port internal;
    Port external;
    IpAddress lan_address;
    std::string description;
};

// Capabilities: none; the seam PortMapperService is tested through.
// UDP entries only. Blocking; called from the WorkerPool. miniupnpc and libnatpmp each implement
// it. No PCP.
class IPortMappingGateway {
public:
    virtual ~IPortMappingGateway() = default;

    [[nodiscard]] virtual MappingMethod method() const noexcept = 0;
    virtual std::expected<GatewayInfo, GatewayError> discover(std::chrono::milliseconds timeout,
                                                              const CancelToken& token) = 0;
    // Returns the entry as granted, whose external port or lease may differ from the request.
    virtual std::expected<PortMapping, GatewayError> add(const GatewayMappingRequest& request,
                                                         std::chrono::milliseconds timeout) = 0;
    // UPnP deletes by external port and NAT-PMP by internal port, so it takes the whole mapping.
    virtual std::expected<void, GatewayError> remove(const PortMapping& mapping, std::chrono::milliseconds timeout) = 0;
    // Unsupported for NAT-PMP.
    virtual std::expected<std::vector<GatewayEntry>, GatewayError> list(std::chrono::milliseconds timeout) = 0;
};

[[nodiscard]] std::unique_ptr<IPortMappingGateway> make_miniupnpc_gateway();
[[nodiscard]] std::unique_ptr<IPortMappingGateway> make_natpmp_gateway();

}  // namespace rb::net
