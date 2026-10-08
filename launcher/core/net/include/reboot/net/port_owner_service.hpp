#pragma once

#include <span>
#include <vector>

#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/net_types.hpp"
#include "reboot/net/our_process.hpp"
#include "reboot/net/port_owner_info.hpp"
#include "reboot/net/port_protocol.hpp"

namespace reboot::ports {
class IPortInspector;
class IProcessLauncher;
}  // namespace reboot::ports

namespace reboot::net {

// Capabilities: matchmaking-networking.kill-by-port, matchmaking-networking.+44.
// Names who holds a port; it never terminates anything, so a busy port is reported, not freed.
// A pid counts as ours only while IProcessLauncher::is_alive confirms its creation time.
// Blocking (the inspector walks the OS socket tables): call it from the WorkerPool.
class PortOwnerService {
public:
    PortOwnerService(ports::IPortInspector& inspector, ports::IProcessLauncher& processes)
        : inspector_(inspector), processes_(processes) {}

    // Every process holding `local` (UDP looks at the port only); empty when none does. Fails
    // only when the socket table cannot be read. An owner whose process cannot be inspected is
    // an Unknown entry with its lookup_error, and the other owners are still classified.
    [[nodiscard]] Result<std::vector<PortOwnerInfo>> owners(PortProtocol protocol, Endpoint local,
                                                            std::span<const OurProcess> ours);

    // Host readiness: whether `process` is among the owners of `local`.
    [[nodiscard]] Result<bool> held_by(PortProtocol protocol, Endpoint local, const OurProcess& process);

private:
    ports::IPortInspector& inspector_;
    ports::IProcessLauncher& processes_;
};

}  // namespace reboot::net
