#pragma once

#include <vector>

#include "reboot/integration/entry_status.hpp"
#include "reboot/integration/integration_targets.hpp"

namespace rb::ports {
class IIntegrationRegistrar;
}

namespace rb::integration {

// Velopack's uninstall hook and AppImage or tarball removal: store-free, never elevated, Foreign kept.
[[nodiscard]] std::vector<EntryStatus> uninstall_integration(ports::IIntegrationRegistrar& registrar,
                                                             const IntegrationTargets& targets);

}  // namespace rb::integration
