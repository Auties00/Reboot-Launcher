#pragma once

#include <vector>

#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/function.hpp"
#include "reboot/foundation/types.hpp"

namespace rb::backend {

// Implemented by the engine over its SessionRegistry, which backend may not depend on.
class IBackendSessions {
public:
    virtual ~IBackendSessions() = default;

    // Stops the sessions with the usual grace; `done` runs on the strand once all of them ended.
    virtual void stop_sessions(std::vector<SessionId> sessions, UniqueFunction<void(Result<void>)> done) = 0;
};

}  // namespace rb::backend
