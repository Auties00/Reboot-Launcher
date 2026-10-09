#pragma once

#include <optional>

#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/net_types.hpp"
#include "reboot/foundation/types.hpp"
#include "reboot/ports/net.hpp"

namespace rb::os_linux::platform {

// Covers no capability ids; ILoopbackPeerInspector for the front's ticket-redemption uid check.
class SockDiagPeerInspector final : public ports::ILoopbackPeerInspector {
public:
    // `local` and `remote` are the front's accepted connection; the peer is the TCP socket whose
    // own local and remote are the reverse. An exact-tuple inet_diag request returns its
    // idiag_uid; /proc/net/tcp{,6} is the fallback where netlink is refused. nullopt when the
    // peer socket is already gone. A peer under Wine reports the uid wineserver runs as.
    Result<std::optional<u32>> peer_uid(Endpoint local, Endpoint remote) override;
};

}  // namespace rb::os_linux::platform
