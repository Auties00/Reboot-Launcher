#pragma once

#include <string>

#include "reboot/foundation/cancel.hpp"
#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/function.hpp"
#include "reboot/foundation/types.hpp"
#include "reboot/gameserver/game_server_config.hpp"

namespace rb::host {

// Capabilities: none; implements game-server-dll-design (a server that declares needs_backend).
// Strand-only. host may not depend on backend, so the engine implements this over
// backend::BackendService: a session lease plus what ServerConfig.backend carries.
class IHostBackendLink {
public:
    virtual ~IHostBackendLink() = default;

    // Leases the backend for the session, registers `account_id` and returns the origin and
    // service token. `done` runs on the strand exactly once; a cancel ends with ErrorKind::Cancelled.
    virtual void acquire(SessionId session, std::string account_id, CancelToken token,
                         UniqueFunction<void(Result<gameserver::BackendAccess>)> done) = 0;
    // Ends the session's lease; a session without one is a no-op.
    virtual void release(SessionId session) = 0;
};

}  // namespace rb::host
