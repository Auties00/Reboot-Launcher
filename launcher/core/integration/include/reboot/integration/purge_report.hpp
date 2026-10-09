#pragma once

#include <vector>

#include "reboot/foundation/native_path.hpp"
#include "reboot/integration/purge_scope.hpp"

namespace rb::integration {

struct PurgeReport {
    PurgeScope scope{};
    std::vector<NativePath> removed;
    // Targets that did not exist.
    std::vector<NativePath> absent;
};

}  // namespace rb::integration
