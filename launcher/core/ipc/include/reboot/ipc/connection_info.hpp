#pragma once

#include <string>

#include "reboot/contracts/ipc.hpp"
#include "reboot/foundation/operation.hpp"
#include "reboot/foundation/types.hpp"
#include "reboot/ipc/compatibility.hpp"
#include "reboot/ports/ipc.hpp"

namespace reboot::ipc {

// One client after Hello; the engine builds api::CallContext from it.
struct ConnectionInfo {
    ConnectionId id;
    // From the listener's OS check, not from Hello.
    ports::PeerIdentity peer;
    contracts::ipc::ClientKind client_kind{};
    std::string client_build;
    u32 abi_version = 0;
    u32 client_pid = 0;
    // Play checks its os_session.
    contracts::ipc::CallerContext caller;
    Compatibility compatibility{};
};

}  // namespace reboot::ipc
