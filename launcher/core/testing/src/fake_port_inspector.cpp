#include "reboot/testing/fake_port_inspector.hpp"

#include <mutex>
#include <optional>
#include <utility>

namespace reboot::testing {

Result<std::optional<ports::PortOwner>> FakePortInspector::tcp_owner(Endpoint local) {
    if (auto error = faults_.take(PortInspectorOperation::TcpOwner)) return std::unexpected(std::move(*error));
    const std::scoped_lock lock(mutex_);
    const auto it = tcp_.find(local);
    if (it == tcp_.end()) return std::optional<ports::PortOwner>();
    return std::optional<ports::PortOwner>(it->second);
}

Result<std::optional<ports::PortOwner>> FakePortInspector::udp_owner(Port port) {
    if (auto error = faults_.take(PortInspectorOperation::UdpOwner)) return std::unexpected(std::move(*error));
    const std::scoped_lock lock(mutex_);
    const auto it = udp_.find(port);
    if (it == udp_.end()) return std::optional<ports::PortOwner>();
    return std::optional<ports::PortOwner>(it->second);
}

void FakePortInspector::set_tcp_owner(Endpoint local, ports::PortOwner owner) {
    const std::scoped_lock lock(mutex_);
    tcp_.insert_or_assign(local, std::move(owner));
}

void FakePortInspector::set_udp_owner(Port port, ports::PortOwner owner) {
    const std::scoped_lock lock(mutex_);
    udp_.insert_or_assign(port, std::move(owner));
}

void FakePortInspector::clear_tcp(Endpoint local) {
    const std::scoped_lock lock(mutex_);
    tcp_.erase(local);
}

void FakePortInspector::clear_udp(Port port) {
    const std::scoped_lock lock(mutex_);
    udp_.erase(port);
}

}  // namespace reboot::testing
