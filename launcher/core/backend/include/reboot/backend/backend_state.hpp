#pragma once

#include <optional>
#include <string>

#include "reboot/backend/backend_config.hpp"
#include "reboot/backend/backend_phase.hpp"
#include "reboot/backend/backend_upstream.hpp"
#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/types.hpp"

namespace reboot::backend {

struct BackendState {
    BackendPhase phase = BackendPhase::Stopped;
    BackendConfig config;
    // Kept running by the user, for LAN serving, with no session.
    bool pinned = false;
    u32 leases = 0;
    // Embedded spawns in this engine's life; replies and replays are tied to it.
    u32 generation = 0;
    // Running only.
    std::optional<BackendUpstream> upstream;
    // The embedded build, or the remote's backend-info version; empty when unknown.
    std::string version;
    // Why the last start failed or the backend crashed; cleared by the next Running.
    std::optional<Diagnostic> last_error;
    // A reconfigure waiting for the last lease; acquire() fails with backend.reconfiguring meanwhile.
    std::optional<BackendConfig> pending_config;
};

}  // namespace reboot::backend
