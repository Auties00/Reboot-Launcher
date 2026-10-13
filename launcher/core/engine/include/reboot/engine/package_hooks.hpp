#pragma once

#include <optional>

#include "reboot/foundation/diag.hpp"
#include "reboot/ports/platform_services.hpp"

namespace rb::ports {
class IIntegrationRegistrar;
class IPlatformPaths;
}  // namespace rb::ports

namespace rb::engine {

// Capabilities: none. One installer hook against the OS integration, store-free and never
// elevated: an install writes the engine agent, an update rewrites only a stale one, and an
// uninstall removes every entry of ours. Returns why the first entry that should have changed
// did not.
[[nodiscard]] std::optional<Diagnostic> run_package_hook(ports::PackageHook hook, ports::IIntegrationRegistrar& registrar,
                                                         const ports::IPlatformPaths& paths);

}  // namespace rb::engine
