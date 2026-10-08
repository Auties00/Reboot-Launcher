#pragma once

#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/function.hpp"
#include "reboot/sessions/stop_request.hpp"

namespace reboot::sessions {

// Runs once on the strand, possibly inside stop(); an error still ends the session.
using StopDone = UniqueFunction<void(Result<void>)>;

// Capabilities: game-launch.instance-registry, game-launch.+5, game-launch.app-exit-cleanup.
// Strand-only. Play and host give one per session they open; the registry owns and destroys it.
class ISessionDriver {
public:
    // Also runs while a stop overran grace + kStopKillMargin, so it must release every handle it holds.
    virtual ~ISessionDriver() = default;

    // Called once. Cancels a pending start, ends every process, deletes per-session files, frees routes and leases.
    virtual void stop(const StopRequest& request, StopDone done) = 0;
};

}  // namespace reboot::sessions
