#pragma once

#include <optional>

#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/net_types.hpp"
#include "reboot/ports/net.hpp"

namespace rb::os_linux::platform {

// Covers no capability ids; IPortInspector over NETLINK_SOCK_DIAG.
class SockDiagPortInspector final : public ports::IPortInspector {
public:
    // An inet_diag dump (AF_INET and AF_INET6) gives the matching socket's inode; a scan of
    // /proc/*/fd for socket:[inode] gives the pid, and /proc/<pid>/exe the exe. Where netlink is
    // refused (some sandboxes), /proc/net/{tcp,tcp6,udp,udp6} give the inode instead. A socket
    // held by wineserver, which owns every socket of a Wine process, reports that wineserver
    // with wine_server set. A socket of another user's process, whose fds cannot be read,
    // reports pid 0 and no exe. A v4 endpoint also matches a dual-stack v6 wildcard listener.
    Result<std::optional<ports::PortOwner>> tcp_owner(Endpoint local) override;
    // UDP sockets bound to `port` on any local address.
    Result<std::optional<ports::PortOwner>> udp_owner(Port port) override;
};

}  // namespace rb::os_linux::platform
