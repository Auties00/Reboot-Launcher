#pragma once

#include "reboot/foundation/diag.hpp"
#include "reboot/ports/platform_services.hpp"

namespace reboot::ports {

// Linux composition: XdgPaths, LinuxFileSystem, InotifyWatcher, LinuxDiskInfo,
// LibsecretSecretStore (fallback files in <data root>/state/secrets), PidfdProcessLauncher
// (scopes when a systemd user manager answers), no session host, SockDiagPortInspector,
// SockDiagPeerInspector, LinuxResolver, XdgShell, LinuxIntegrationRegistrar,
// NoSecurityProductProbe, LinuxPrerequisiteProbe, LinuxSystemInfo, LinuxUpdateApplier and
// OsRandom. The data root is options.data_root_override or XdgPaths' default. runner is left to
// core-linux/runner and ipc_listener to core-linux/ipc.
[[nodiscard]] Result<PlatformServices> make_platform(const PlatformOptions& options);

}  // namespace reboot::ports
