#pragma once

#include <map>
#include <mutex>
#include <optional>

#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/net_types.hpp"
#include "reboot/foundation/types.hpp"
#include "reboot/ports/net.hpp"
#include "reboot/testing/fault_plan.hpp"

namespace rb::testing {

enum class PortInspectorOperation : u8 { TcpOwner, UdpOwner };

// Covers no capability ids (decision testing-strategy).
// IPortInspector over owners the test sets: host readiness checks that the game server's pid owns
// each port, and PortBusy names an owner. An unset port has no owner.
class FakePortInspector final : public ports::IPortInspector {
public:
    Result<std::optional<ports::PortOwner>> tcp_owner(Endpoint local) override;
    Result<std::optional<ports::PortOwner>> udp_owner(Port port) override;

    void set_tcp_owner(Endpoint local, ports::PortOwner owner);
    void set_udp_owner(Port port, ports::PortOwner owner);
    void clear_tcp(Endpoint local);
    void clear_udp(Port port);

    [[nodiscard]] FaultPlan<PortInspectorOperation>& faults() noexcept { return faults_; }

private:
    mutable std::mutex mutex_;
    std::map<Endpoint, ports::PortOwner> tcp_;
    std::map<Port, ports::PortOwner> udp_;
    FaultPlan<PortInspectorOperation> faults_;
};

}  // namespace rb::testing
