#pragma once

#include "reboot/components/component_pin.hpp"
#include "reboot/components/payload_set.hpp"

namespace reboot::components {

struct PinnedPayload {
    PayloadSet set;
    ComponentPin pin;
};

}  // namespace reboot::components
