#include "reboot/integration/install_integration.hpp"

#include "entry_ops.hpp"

namespace reboot::integration {

EntryStatus install_integration(ports::IIntegrationRegistrar& registrar, const IntegrationTargets& targets,
                                InstallHook hook) {
    const EntryStatus found = inspect_entry(registrar, targets, IntegrationKind::EngineAgent);
    if (hook == InstallHook::AfterUpdate && found.state != EntryState::Stale) return found;
    return apply_entry(registrar, targets, found);
}

}  // namespace reboot::integration
