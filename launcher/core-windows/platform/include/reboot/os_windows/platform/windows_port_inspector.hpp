#pragma once

#include <optional>

#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/net_types.hpp"
#include "reboot/ports/net.hpp"

namespace reboot::os_windows::platform {

// Covers no capability ids; IPortInspector over the IP Helper owner tables.
class WindowsPortInspector final : public ports::IPortInspector {
public:
    // A v4 endpoint also matches a dual-stack v6 wildcard listener.
    Result<std::optional<ports::PortOwner>> tcp_owner(Endpoint local) override;
    Result<std::optional<ports::PortOwner>> udp_owner(Port port) override;
};

}  // namespace reboot::os_windows::platform
