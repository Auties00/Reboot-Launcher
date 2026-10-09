#pragma once

#include <optional>

#include "reboot/foundation/diag.hpp"
#include "reboot/integration/prerequisite_id.hpp"
#include "reboot/integration/prerequisite_impact.hpp"
#include "reboot/integration/remedy.hpp"

namespace rb::integration {

struct Prerequisite {
    PrerequisiteId id{};
    bool met = false;
    PrerequisiteImpact impact{};
    Remedy remedy{};
    MessageId guidance;
    // The probe's own, more specific hint, e.g. the distro package to install.
    std::optional<MessageId> platform_hint;
};

}  // namespace rb::integration
