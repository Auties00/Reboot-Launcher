#pragma once

#include "reboot/foundation/diag.hpp"
#include "reboot/ux/guidance_state.hpp"

namespace rb::ux {

// Reads and writes GuidanceState; the engine adapts storage's state document to it.
class IGuidanceStateStore {
public:
    virtual ~IGuidanceStateStore() = default;

    [[nodiscard]] virtual const GuidanceState& current() const = 0;
    // Written through before returning; a failure leaves current() unchanged.
    virtual Result<void> replace(GuidanceState state) = 0;
};

}  // namespace rb::ux
