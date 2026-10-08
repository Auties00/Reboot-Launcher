#pragma once

#include <optional>
#include <string>

#include "reboot/foundation/diag.hpp"
#include "reboot/integration/entry_state.hpp"
#include "reboot/integration/integration_kind.hpp"

namespace reboot::integration {

// Read from the entry itself each time; only `declined` comes from state.json.
struct EntryStatus {
    IntegrationKind kind{};
    EntryState state = EntryState::Unknown;
    // The user removed it, so a reconcile leaves it absent.
    bool declined = false;
    // What the entry runs or points at, as the OS reports it; empty when it reports none.
    std::string target;
    std::optional<Diagnostic> detail;
};

}  // namespace reboot::integration
