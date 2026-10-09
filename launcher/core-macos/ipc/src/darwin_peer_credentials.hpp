#pragma once

#include "reboot/foundation/diag.hpp"
#include "reboot/posix/peer_credential_check.hpp"

namespace rb::os_macos::ipc {

// posix::PeerCredentialCheck::Reader for macOS: getpeereid for the uid, LOCAL_PEERPID for the pid.
[[nodiscard]] Result<posix::PeerCredentials> read_darwin_peer(int socket_fd);

}  // namespace rb::os_macos::ipc
