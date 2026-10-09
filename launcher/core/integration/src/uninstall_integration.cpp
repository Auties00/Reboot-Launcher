#include "reboot/integration/uninstall_integration.hpp"

#include <utility>

#include "entry_ops.hpp"

namespace reboot::integration {

std::vector<EntryStatus> uninstall_integration(ports::IIntegrationRegistrar& registrar,
                                               const IntegrationTargets& targets) {
    std::vector<EntryStatus> statuses;
    statuses.reserve(kAllIntegrationKinds.size());
    for (const IntegrationKind kind : kAllIntegrationKinds) {
        EntryStatus found = inspect_entry(registrar, targets, kind);
        // Another app's entry and a kind this OS lacks are expected here, not failures.
        if (found.state == EntryState::Foreign || found.state == EntryState::Unsupported) {
            statuses.push_back(std::move(found));
            continue;
        }
        statuses.push_back(remove_entry(registrar, targets, found));
    }
    return statuses;
}

}  // namespace reboot::integration
