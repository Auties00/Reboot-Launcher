#pragma once

#include <chrono>
#include <optional>
#include <vector>

#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/net_types.hpp"
#include "reboot/foundation/types.hpp"

namespace reboot::net {

enum class MappingMethod : u8 { Upnp, NatPmp };

// Always UDP. A lease of 0 means the gateway only grants permanent entries (UPnP 725); we
// delete it on stop.
struct PortMapping {
    Port internal;
    Port external;
    MappingMethod method = MappingMethod::Upnp;
    std::chrono::seconds lease{0};
    IpAddress lan_address;
};

// Published as EventKind::PortMappingChanged on every map, renewal that moves a port, failure and
// unmap. `mappings` holds the ports that mapped, so next to `failure` it may lack some.
struct PortMappingChanged {
    SessionId session;
    // The block's first (game) port.
    Port game_port;
    std::vector<PortMapping> mappings;
    std::optional<Diagnostic> failure;

    // The external port to advertise for the game socket, when it mapped.
    [[nodiscard]] std::optional<Port> granted_game_port() const {
        for (const PortMapping& mapping : mappings)
            if (mapping.internal == game_port) return mapping.external;
        return std::nullopt;
    }
};

}  // namespace reboot::net
