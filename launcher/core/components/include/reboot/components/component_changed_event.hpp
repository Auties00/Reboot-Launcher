#pragma once

#include <optional>

#include "reboot/components/component_info.hpp"
#include "reboot/components/component_problem.hpp"

namespace reboot::components {

// EventKind::ComponentChanged, coalesced per component id; `problem` stays set through recovery.
struct ComponentChangedEvent {
    ComponentInfo info;
    std::optional<ComponentProblem> problem;
};

}  // namespace reboot::components
