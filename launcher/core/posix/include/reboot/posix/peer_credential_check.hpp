#pragma once

#include <utility>

#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/function.hpp"
#include "reboot/foundation/types.hpp"
#include "reboot/ports/ipc.hpp"

namespace reboot::posix {

struct PeerCredentials {
    u32 uid = 0;
    // 0 when the OS reports no pid for the peer.
    u32 pid = 0;
};

// Covers no capability ids; the peer-uid rule shared by the AF_UNIX listener and connector.
class PeerCredentialCheck {
public:
    // getpeereid (plus LOCAL_PEERPID) on macOS, SO_PEERCRED on Linux.
    using Reader = UniqueFunction<Result<PeerCredentials>(int socket_fd)>;

    // `expected_uid` is the calling process's effective uid.
    PeerCredentialCheck(Reader read_peer, u32 expected_uid) noexcept
        : read_peer_(std::move(read_peer)), expected_uid_(expected_uid) {}

    // Fails with ipc.endpoint_untrusted, caused by posix.peer_other_user when the peer is another
    // user, or by the reader's failure when its credentials cannot be read.
    [[nodiscard]] Result<ports::PeerIdentity> verify(int socket_fd);

    [[nodiscard]] u32 expected_uid() const noexcept { return expected_uid_; }

private:
    Reader read_peer_;
    u32 expected_uid_;
};

}  // namespace reboot::posix
