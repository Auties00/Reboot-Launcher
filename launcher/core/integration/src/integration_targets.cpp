#include "reboot/integration/integration_targets.hpp"

namespace reboot::integration {

std::optional<NativePath> entry_program(IntegrationKind kind, const IntegrationTargets& targets) {
    switch (kind) {
        case IntegrationKind::UrlScheme: return targets.gui_exe;
        case IntegrationKind::DesktopEntry:
            if (targets.flavor != EntryFlavor::FreeDesktop) return std::nullopt;
            return targets.gui_exe;
        case IntegrationKind::Autostart:
        case IntegrationKind::EngineAgent: return targets.engine_exe;
    }
    return std::nullopt;
}

}  // namespace reboot::integration
