#pragma once

#include "reboot/integration/entry_status.hpp"
#include "reboot/integration/integration_kind.hpp"
#include "reboot/integration/integration_targets.hpp"

namespace rb::ports {
class IIntegrationRegistrar;
}

namespace rb::integration {

// Blocking registrar calls, for workers and the store-free hooks; `declined` is left false.

[[nodiscard]] EntryStatus inspect_entry(ports::IIntegrationRegistrar& registrar, const IntegrationTargets& targets,
                                        IntegrationKind kind);

// Writes `found` and reads it back; an entry that is still not registered gets NotApplied.
[[nodiscard]] EntryStatus write_entry(ports::IIntegrationRegistrar& registrar, const IntegrationTargets& targets,
                                      const EntryStatus& found);

// Writes only Absent and Stale entries; Foreign and Unsupported ones get their reason in `detail`.
[[nodiscard]] EntryStatus apply_entry(ports::IIntegrationRegistrar& registrar, const IntegrationTargets& targets,
                                      const EntryStatus& found);

// Removes only entries of ours; Foreign and Unsupported ones get their reason in `detail`.
[[nodiscard]] EntryStatus remove_entry(ports::IIntegrationRegistrar& registrar, const IntegrationTargets& targets,
                                       const EntryStatus& found);

}  // namespace rb::integration
