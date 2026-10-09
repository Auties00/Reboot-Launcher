#pragma once

#include <optional>

#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/net_types.hpp"
#include "reboot/ports/net.hpp"

namespace rb::os_macos::platform {

// Covers no capability ids; IPortInspector over libproc; without root, other users' ports are nullopt.
class LibprocPortInspector final : public ports::IPortInspector {
public:
    // A dual-stack v6 wildcard listener also owns a v4 `local`.
    Result<std::optional<ports::PortOwner>> tcp_owner(Endpoint local) override;
    Result<std::optional<ports::PortOwner>> udp_owner(Port port) override;
};

}  // namespace rb::os_macos::platform
