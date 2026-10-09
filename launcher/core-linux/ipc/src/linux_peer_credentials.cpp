#include "linux_peer_credentials.hpp"

#include <cerrno>
#include <sys/socket.h>

#include "reboot/posix/posix_error.hpp"

namespace rb::os_linux::ipc {

Result<posix::PeerCredentials> read_linux_peer(int socket_fd) {
    ucred credentials{};
    socklen_t length = sizeof credentials;
    if (::getsockopt(socket_fd, SOL_SOCKET, SO_PEERCRED, &credentials, &length) != 0)
        return std::unexpected(posix::call_failed("getsockopt", errno));
    // A peer in another pid namespace reads as pid 0, which PeerCredentials keeps as "no pid".
    return posix::PeerCredentials{.uid = static_cast<u32>(credentials.uid),
                                  .pid = credentials.pid > 0 ? static_cast<u32>(credentials.pid) : 0U};
}

}  // namespace rb::os_linux::ipc
