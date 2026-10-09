#pragma once

#include <vector>

#include "reboot/integration/entry_status.hpp"

namespace rb::integration {

// EventKind::IntegrationChanged, coalesced: every entry as read after an apply, remove or reconcile.
struct IntegrationChanged {
    std::vector<EntryStatus> entries;
};

}  // namespace rb::integration
