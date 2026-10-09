#pragma once

#include <vector>

#include "reboot/integration/prerequisite.hpp"

namespace rb::integration {

// EventKind::PrerequisitesChanged, coalesced: every prerequisite as re-checked after a remediation.
struct PrerequisitesChanged {
    std::vector<Prerequisite> prerequisites;
};

}  // namespace rb::integration
