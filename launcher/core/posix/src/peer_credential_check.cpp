#include "reboot/posix/peer_credential_check.hpp"

#include <string>
#include <utility>

#include "messages.hpp"

namespace reboot::posix {

Result<ports::PeerIdentity> PeerCredentialCheck::verify(int socket_fd) {
    auto peer = read_peer_(socket_fd);
    if (!peer) return make_diag(ErrorDomain::Ipc, kEndpointUntrusted).cause(std::move(peer.error())).fail();
    if (peer->uid != expected_uid_) {
        return make_diag(ErrorDomain::Ipc, kEndpointUntrusted)
            .cause(make_diag(ErrorDomain::Posix, kPeerOtherUser)
                       .arg("peer_uid", peer->uid)
                       .arg("expected_uid", expected_uid_))
            .fail();
    }
    return ports::PeerIdentity{std::to_string(peer->uid), peer->pid};
}

}  // namespace reboot::posix
