#include "darwin_peer_credentials.hpp"

#include "unistd.hpp"

#include <sys/socket.h>
#include <sys/types.h>
#include <sys/un.h>

#include <cerrno>

#include "reboot/posix/posix_error.hpp"

namespace rb::os_macos::ipc {

Result<posix::PeerCredentials> read_darwin_peer(int socket_fd) {
    uid_t uid = 0;
    gid_t gid = 0;
    if (::getpeereid(socket_fd, &uid, &gid) != 0) return std::unexpected(posix::call_failed("getpeereid", errno));
    // The uid is what the check needs; a peer whose pid cannot be read keeps pid 0.
    pid_t pid = 0;
    socklen_t length = sizeof pid;
    if (::getsockopt(socket_fd, SOL_LOCAL, LOCAL_PEERPID, &pid, &length) != 0 || length != sizeof pid) pid = 0;
    return posix::PeerCredentials{.uid = static_cast<u32>(uid), .pid = pid > 0 ? static_cast<u32>(pid) : 0U};
}

}  // namespace rb::os_macos::ipc
