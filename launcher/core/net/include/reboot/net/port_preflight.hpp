#pragma once

#include <expected>
#include <memory>
#include <span>

#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/net_types.hpp"
#include "reboot/foundation/types.hpp"
#include "reboot/net/our_process.hpp"
#include "reboot/net/port_conflict.hpp"
#include "reboot/net/port_protocol.hpp"

namespace boost::asio {
class io_context;
}

namespace reboot::net {

class PortOwnerService;

enum class PortAvailability : u8 { Free, InUse, AccessDenied };

// Capabilities: matchmaking-networking.kill-by-port.
// Bind test on the address the real listener will use (the IPv4 wildcard for game ports,
// 127.0.0.1 for the legacy fixed ports) without SO_REUSEADDR (SO_EXCLUSIVEADDRUSE on Windows),
// closed at once, so a port shared through address reuse still counts as taken. Blocking only
// for the bind itself; the owner lookup in require_free() walks socket tables, so call that from
// the WorkerPool.
class PortPreflight {
public:
    PortPreflight(boost::asio::io_context& io, PortOwnerService& owners);
    ~PortPreflight();
    PortPreflight(const PortPreflight&) = delete;
    PortPreflight& operator=(const PortPreflight&) = delete;

    // Fails only when no socket can be opened at all.
    [[nodiscard]] Result<PortAvailability> test(PortProtocol protocol, Endpoint bind);

    // Tests every port of a block on `address` and names the owners of the first one taken.
    [[nodiscard]] Result<std::expected<void, PortConflict>> require_free(PortProtocol protocol, IpAddress address,
                                                                         std::span<const Port> block,
                                                                         std::span<const OurProcess> ours);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace reboot::net
