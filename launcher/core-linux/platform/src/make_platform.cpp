#include "reboot/os_linux/platform/make_platform.hpp"

#include <memory>
#include <utility>

#include "helper_process.hpp"
#include "linux_log_file_system.hpp"
#include "process_environment.hpp"
#include "reboot/foundation/paths.hpp"
#include "reboot/foundation/random.hpp"
#include "reboot/os_linux/platform/inotify_watcher.hpp"
#include "reboot/os_linux/platform/libsecret_secret_store.hpp"
#include "reboot/os_linux/platform/linux_disk_info.hpp"
#include "reboot/os_linux/platform/linux_file_system.hpp"
#include "reboot/os_linux/platform/linux_integration_registrar.hpp"
#include "reboot/os_linux/platform/linux_prerequisite_probe.hpp"
#include "reboot/os_linux/platform/linux_resolver.hpp"
#include "reboot/os_linux/platform/linux_system_info.hpp"
#include "reboot/os_linux/platform/linux_update_applier.hpp"
#include "reboot/os_linux/platform/no_security_product_probe.hpp"
#include "reboot/os_linux/platform/pidfd_process_launcher.hpp"
#include "reboot/os_linux/platform/sock_diag_peer_inspector.hpp"
#include "reboot/os_linux/platform/sock_diag_port_inspector.hpp"
#include "reboot/os_linux/platform/xdg_paths.hpp"
#include "reboot/os_linux/platform/xdg_shell.hpp"
#include "systemd_user.hpp"

namespace reboot::ports {

namespace {

[[nodiscard]] std::optional<os_linux::platform::PidfdProcessLauncher::SystemdScopes> systemd_scopes(
    const os_linux::platform::XdgPaths& paths) {
    using namespace os_linux::platform;
    const std::optional<NativePath>& runtime_dir = paths.runtime_dir();
    if (!runtime_dir || !systemd_user_manager_answers(*runtime_dir)) return std::nullopt;
    std::optional<NativePath> systemd_run = find_in_path("systemd-run", env_value("PATH").value_or(""));
    if (!systemd_run) systemd_run = find_in_path("systemd-run", "/usr/bin:/bin");
    if (!systemd_run) return std::nullopt;
    return PidfdProcessLauncher::SystemdScopes{.systemd_run = std::move(*systemd_run), .runtime_dir = *runtime_dir};
}

}  // namespace

Result<PlatformServices> make_platform(const PlatformOptions& options) {
    using namespace os_linux::platform;
    Result<XdgPaths> detected = XdgPaths::detect();
    if (!detected) return std::unexpected(std::move(detected.error()));
    auto paths = std::make_unique<XdgPaths>(std::move(*detected));

    const DataRoot root = options.data_root_override ? DataRoot{*options.data_root_override, true}
                                                     : DataRoot{paths->default_data_root(), false};
    const NativePath canonical = canonical_root(root);
    const std::string hash16 = root_hash16(canonical);

    PlatformServices services;
    auto fs = std::make_unique<LinuxFileSystem>();
    services.secrets = std::make_unique<LibsecretSecretStore>(hash16, canonical / "state" / "secrets", *fs);
    services.fs = std::move(fs);
    services.logs = std::make_unique<LinuxLogFileSystem>();
    services.watcher = std::make_unique<InotifyWatcher>();
    services.disk = std::make_unique<LinuxDiskInfo>();
    services.processes = std::make_unique<PidfdProcessLauncher>(systemd_scopes(*paths));
    services.ports = std::make_unique<SockDiagPortInspector>();
    services.peer_inspector = std::make_unique<SockDiagPeerInspector>();
    services.resolver = std::make_unique<LinuxResolver>();
    services.shell = std::make_unique<XdgShell>();
    services.integration = std::make_unique<LinuxIntegrationRegistrar>(*paths, hash16, options.data_root_override);
    services.security = std::make_unique<NoSecurityProductProbe>();
    services.prerequisites = std::make_unique<LinuxPrerequisiteProbe>(paths->user_name());
    auto system = std::make_unique<LinuxSystemInfo>();
    services.updater = std::make_unique<LinuxUpdateApplier>(*paths, system->in_container());
    services.system = std::move(system);
    services.random = std::make_unique<OsRandom>();
    services.paths = std::move(paths);
    return services;
}

}  // namespace reboot::ports
