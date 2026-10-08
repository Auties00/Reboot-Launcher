#pragma once

#include <optional>

#include "reboot/compat/runtime_id.hpp"
#include "reboot/compat/slr_install.hpp"

namespace reboot::compat {

// What the engine learned about one stored runtime.
struct RuntimeRecord {
    RuntimeId runtime;
    // A session completed on it; ends the Rosetta first-run multiplier.
    bool completed_session = false;
    // Umu's launcher runtime only: the last setup run with it.
    std::optional<SlrInstall> slr;

    bool operator==(const RuntimeRecord&) const = default;
};

}  // namespace reboot::compat
