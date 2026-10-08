#pragma once

#include <optional>
#include <vector>

#include "reboot/foundation/version.hpp"
#include "reboot/integration/entry_status.hpp"
#include "reboot/integration/integration_kind.hpp"

namespace reboot::integration {

struct ReconcileReport {
    // nullopt on the first start ever.
    std::optional<SemVer> previous;
    SemVer running;
    std::vector<IntegrationKind> written;
    // Every kind after the pass; a failed write carries its Diagnostic in `detail`.
    std::vector<EntryStatus> items;
};

}  // namespace reboot::integration
