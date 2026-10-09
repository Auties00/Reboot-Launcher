#include "darwin.hpp"

#include "reboot/os_macos/platform/make_platform.hpp"

#include <cstdlib>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include "integration_rules.hpp"
#include "mac_log_file_system.hpp"
#include "reboot/foundation/paths.hpp"
#include "reboot/foundation/random.hpp"
#include "reboot/os_macos/platform/fs_events_watcher.hpp"
#include "reboot/os_macos/platform/keychain_secret_store.hpp"
#include "reboot/os_macos/platform/libproc_port_inspector.hpp"
#include "reboot/os_macos/platform/mac_disk_info.hpp"
#include "reboot/os_macos/platform/mac_file_system.hpp"
#include "reboot/os_macos/platform/mac_integration_registrar.hpp"
#include "reboot/os_macos/platform/mac_paths.hpp"
#include "reboot/os_macos/platform/mac_prerequisite_probe.hpp"
#include "reboot/os_macos/platform/mac_resolver.hpp"
#include "reboot/os_macos/platform/mac_shell.hpp"
#include "reboot/os_macos/platform/mac_system_info.hpp"
#include "reboot/os_macos/platform/mac_velopack_applier.hpp"
#include "reboot/os_macos/platform/no_security_product_probe.hpp"
#include "reboot/os_macos/platform/posix_spawn_launcher.hpp"
#include "reboot/os_macos/platform/unsupported_peer_inspector.hpp"

namespace rb::ports {

namespace {

[[nodiscard]] std::optional<std::string_view> environment(const char* name) {
    const char* value = std::getenv(name);
    if (value == nullptr) return std::nullopt;
    return std::string_view(value);
}

[[nodiscard]] std::optional<std::string> launchd_label(bool foreground) {
    if (foreground) return std::nullopt;
    return os_macos::platform::own_agent_label(environment("XPC_SERVICE_NAME"));
}

}  // namespace

Result<PlatformServices> make_platform(const PlatformOptions& options) {
    using namespace os_macos::platform;

    Result<MacPaths> detected = MacPaths::detect();
    if (!detected) return std::unexpected(std::move(detected.error()));
    auto paths = std::make_unique<MacPaths>(std::move(*detected));

    DataRoot root;
    if (options.data_root_override) {
        root = DataRoot{*options.data_root_override, true};
    } else {
        Result<DataRoot> resolved = resolve_data_root(*paths, environment("REBOOT_LAUNCHER_HOME"));
        if (!resolved) return std::unexpected(std::move(resolved.error()));
        root = std::move(*resolved);
    }
    const AppLayout layout(root, *paths);
    const NativePath cache_dir = layout.catalog_cache().parent_path();

    PlatformServices services;
    auto fs = std::make_unique<MacFileSystem>();
    auto system = std::make_unique<MacSystemInfo>(cache_dir / "trust", *fs);
    const bool aqua = system->aqua();
    std::optional<NativePath> game_server_exe;
    if (paths->install_kind() == InstallKind::AppBundle) game_server_exe = locate_install(*paths, std::nullopt).game_server_exe;

    services.secrets = std::make_unique<KeychainSecretStore>(root_hash16(canonical_root(root)),
                                                             layout.root() / "state" / "secrets", *fs, aqua);
    services.logs = std::make_unique<MacLogFileSystem>();
    services.watcher = std::make_unique<FsEventsWatcher>();
    services.disk = std::make_unique<MacDiskInfo>();
    services.processes = std::make_unique<PosixSpawnLauncher>();
    services.ports = std::make_unique<LibprocPortInspector>();
    services.peer_inspector = std::make_unique<UnsupportedPeerInspector>();
    services.resolver = std::make_unique<MacResolver>();
    services.shell = std::make_unique<MacShell>(aqua);
    services.integration = std::make_unique<MacIntegrationRegistrar>(paths->app_bundle(), paths->translocated());
    services.security = std::make_unique<NoSecurityProductProbe>();
    services.prerequisites = std::make_unique<MacPrerequisiteProbe>(std::move(game_server_exe));
    services.updater = std::make_unique<MacVelopackApplier>(paths->velopack_package_dir(), cache_dir / "velopack",
                                                            launchd_label(options.foreground));
    services.random = std::make_unique<OsRandom>();
    services.system = std::move(system);
    services.fs = std::move(fs);
    services.paths = std::move(paths);
    return services;
}

}  // namespace rb::ports
