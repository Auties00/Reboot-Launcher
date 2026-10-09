#pragma once

#include "reboot/foundation/types.hpp"
#include "reboot/integration/entry_status.hpp"
#include "reboot/integration/integration_targets.hpp"

namespace rb::ports {
class IIntegrationRegistrar;
}

namespace rb::integration {

// An engine denied breakaway starts only through the agent, so Velopack's hooks write it.
enum class InstallHook : u8 { AfterInstall, AfterUpdate };

// Store-free; AfterUpdate rewrites only a Stale agent, since an absent one may be the user's removal.
[[nodiscard]] EntryStatus install_integration(ports::IIntegrationRegistrar& registrar,
                                              const IntegrationTargets& targets, InstallHook hook);

}  // namespace rb::integration
