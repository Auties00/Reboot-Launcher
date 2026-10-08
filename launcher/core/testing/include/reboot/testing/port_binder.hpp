#pragma once

#include <memory>
#include <utility>

#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/net_types.hpp"
#include "reboot/foundation/types.hpp"

namespace reboot::testing {

class FakeLoopbackPeerInspector;
class FakePortInspector;

// Covers no capability ids (decision testing-strategy).
// Holds ports for the inspector suites; everything it bound is released on destruction.
class IPortBinder {
public:
    virtual ~IPortBinder() = default;

    virtual Result<Endpoint> bind_tcp() = 0;
    virtual Result<Port> bind_udp() = 0;
    // A connected loopback TCP pair: {our end, the peer's end}.
    virtual Result<std::pair<Endpoint, Endpoint>> connect_loopback() = 0;
    // Drops what bind_tcp or bind_udp bound on `port`.
    virtual void release(Port port) = 0;
    [[nodiscard]] virtual u32 owner_pid() const = 0;
};

// Loopback sockets owned by this process, through Boost.Asio.
[[nodiscard]] std::unique_ptr<IPortBinder> make_socket_port_binder();
// Plants each "bound" port's owner (`pid`) and each pair's peer uid in the fakes.
[[nodiscard]] std::unique_ptr<IPortBinder> make_fake_port_binder(FakePortInspector& ports,
                                                                 FakeLoopbackPeerInspector& peers, u32 pid, u32 uid);

}  // namespace reboot::testing
