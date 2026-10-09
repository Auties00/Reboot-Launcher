#pragma once

#include "reboot/foundation/diag.hpp"
#include "reboot/posix/peer_credential_check.hpp"

namespace rb::os_linux::ipc {

// posix::PeerCredentialCheck::Reader for Linux: getsockopt(SO_PEERCRED) for the uid and pid.
[[nodiscard]] Result<posix::PeerCredentials> read_linux_peer(int socket_fd);

}  // namespace rb::os_linux::ipc
