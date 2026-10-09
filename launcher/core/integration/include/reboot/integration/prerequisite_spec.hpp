#pragma once

#include "reboot/foundation/diag.hpp"
#include "reboot/integration/prerequisite_id.hpp"
#include "reboot/integration/prerequisite_impact.hpp"
#include "reboot/integration/remedy.hpp"

namespace rb::integration {

struct PrerequisiteSpec {
    PrerequisiteId id{};
    PrerequisiteImpact impact{};
    Remedy remedy{};
    MessageId guidance;
};

[[nodiscard]] const PrerequisiteSpec& prerequisite_spec(PrerequisiteId id) noexcept;

}  // namespace rb::integration
