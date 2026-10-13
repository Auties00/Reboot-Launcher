#include "reboot/engine/package_hooks.hpp"

#include <string>
#include <vector>

#include "host_platform.hpp"
#include "reboot/integration/install_integration.hpp"
#include "reboot/integration/uninstall_integration.hpp"
#include "reboot/ports/platform_paths.hpp"

namespace rb::engine {

std::optional<Diagnostic> run_package_hook(ports::PackageHook hook, ports::IIntegrationRegistrar& registrar,
                                           const ports::IPlatformPaths& paths) {
    const integration::IntegrationTargets targets = integration_targets(paths.exe_dir() / std::string(kEngineExe));
    std::vector<integration::EntryStatus> statuses;
    switch (hook) {
        case ports::PackageHook::AfterInstall:
            statuses.push_back(integration::install_integration(registrar, targets, integration::InstallHook::AfterInstall));
            break;
        case ports::PackageHook::AfterUpdate:
            statuses.push_back(integration::install_integration(registrar, targets, integration::InstallHook::AfterUpdate));
            break;
        case ports::PackageHook::BeforeUninstall: statuses = integration::uninstall_integration(registrar, targets); break;
    }
    // Another app's entry and one this OS has no place for are left alone on purpose.
    for (integration::EntryStatus& status : statuses)
        if (status.detail && status.state != integration::EntryState::Foreign &&
            status.state != integration::EntryState::Unsupported)
            return std::move(status.detail);
    return std::nullopt;
}

}  // namespace rb::engine
