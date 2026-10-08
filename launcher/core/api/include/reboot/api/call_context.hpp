#pragma once

#include "reboot/contracts/ipc.hpp"
#include "reboot/foundation/operation.hpp"

namespace reboot::api {

// Who made a call: Play checks the caller's OS session, and a Client lease binds to the connection.
struct CallContext {
    ConnectionId connection;
    contracts::ipc::ClientKind client_kind{};
    // Captured once in Hello; outlives the call.
    const contracts::ipc::CallerContext& caller;
};

}  // namespace reboot::api
