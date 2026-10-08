#pragma once

#include <vector>

#include "reboot/foundation/types.hpp"

namespace reboot::integration {

struct PurgeBlockers {
    std::vector<SessionId> sessions;
    // Live ops that write into the scope: ComponentEnsure, RuntimeSetup, UpdateApply, and play or host preflights.
    std::vector<OpId> ops;
    bool backend_running = false;

    [[nodiscard]] bool empty() const noexcept { return sessions.empty() && ops.empty() && !backend_running; }
};

}  // namespace reboot::integration
