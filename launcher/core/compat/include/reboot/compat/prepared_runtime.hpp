#pragma once

#include <optional>

#include "reboot/compat/prefix_lease.hpp"
#include "reboot/compat/runner_profile.hpp"
#include "reboot/components/pinned_runtime.hpp"
#include "reboot/ports/runner.hpp"

namespace rb::compat {

// One play session's runner, pinned at preflight so a runtime update reaches new sessions only.
// Move-only: it holds the component pins and the prefix lease until the session ends.
struct PreparedRuntime {
    RunnerProfile profile;
    ports::RuntimeLayout layout;
    components::PinnedRuntime wine;
    // Umu only.
    std::optional<components::PinnedRuntime> launcher;
    PrefixLease lease;
};

}  // namespace rb::compat
