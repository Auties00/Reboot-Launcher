#pragma once

#include <vector>

#include "reboot/integration/entry_status.hpp"

namespace reboot::integration {

// EventKind::IntegrationChanged, coalesced: every entry as read after an apply, remove or reconcile.
struct IntegrationChanged {
    std::vector<EntryStatus> entries;
};

}  // namespace reboot::integration
