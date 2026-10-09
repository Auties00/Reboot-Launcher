#include "reboot/os_linux/platform/sock_diag_port_inspector.hpp"

#include <utility>
#include <vector>

#include "inet_diag.hpp"
#include "socket_owner.hpp"
#include "socket_table.hpp"

namespace reboot::os_linux::platform {

namespace {

[[nodiscard]] std::vector<SocketRecord> sockets(DiagProtocol protocol) {
    if (std::optional<std::vector<SocketRecord>> dumped = diag_dump(protocol)) return std::move(*dumped);
    return protocol == DiagProtocol::Tcp ? read_proc_net("tcp", "tcp6") : read_proc_net("udp", "udp6");
}

}  // namespace

Result<std::optional<ports::PortOwner>> SockDiagPortInspector::tcp_owner(Endpoint local) {
    const std::optional<SocketRecord> socket = find_tcp_owner(sockets(DiagProtocol::Tcp), local);
    if (!socket) return std::nullopt;
    return socket_owner(socket->inode);
}

Result<std::optional<ports::PortOwner>> SockDiagPortInspector::udp_owner(Port port) {
    const std::optional<SocketRecord> socket = find_udp_owner(sockets(DiagProtocol::Udp), port);
    if (!socket) return std::nullopt;
    return socket_owner(socket->inode);
}

}  // namespace reboot::os_linux::platform
