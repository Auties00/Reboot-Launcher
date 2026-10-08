#pragma once

#include "reboot/components/component_pin.hpp"
#include "reboot/components/installed_runtime.hpp"

namespace reboot::components {

struct PinnedRuntime {
    InstalledRuntime runtime;
    ComponentPin pin;
};

}  // namespace reboot::components
