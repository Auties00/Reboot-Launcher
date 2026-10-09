#include "reboot/os_linux/platform/sock_diag_peer_inspector.hpp"

#include "inet_diag.hpp"
#include "socket_owner.hpp"
#include "socket_table.hpp"

namespace reboot::os_linux::platform {

Result<std::optional<u32>> SockDiagPeerInspector::peer_uid(Endpoint local, Endpoint remote) {
    // The peer's own local end is the front's remote end.
    if (const DiagLookup lookup = diag_find_tcp(remote, local); lookup.answered) {
        if (!lookup.socket) return std::nullopt;
        return lookup.socket->uid;
    }
    const std::optional<SocketRecord> socket = find_connection(read_proc_net("tcp", "tcp6"), remote, local);
    if (!socket) return std::nullopt;
    return socket->uid;
}

}  // namespace reboot::os_linux::platform
