#include "win32.hpp"  // first: sets _WIN32_WINNT before any std header pulls in <windows.h>

#include "reboot/os_windows/platform/make_platform.hpp"

#include <cstdlib>
#include <memory>
#include <optional>
#include <string>

#include "process_token.hpp"
#include "reboot/foundation/paths.hpp"
#include "reboot/foundation/random.hpp"
#include "reboot/os_windows/platform/credential_manager_store.hpp"
#include "reboot/os_windows/platform/job_process_launcher.hpp"
#include "reboot/os_windows/platform/unsupported_peer_inspector.hpp"
#include "reboot/os_windows/platform/velopack_applier.hpp"
#include "reboot/os_windows/platform/win32_session_host.hpp"
#include "reboot/os_windows/platform/windows_disk_info.hpp"
#include "reboot/os_windows/platform/windows_file_system.hpp"
#include "reboot/os_windows/platform/windows_file_watcher.hpp"
#include "reboot/os_windows/platform/windows_integration_registrar.hpp"
#include "reboot/os_windows/platform/windows_log_file_system.hpp"
#include "reboot/os_windows/platform/windows_paths.hpp"
#include "reboot/os_windows/platform/windows_port_inspector.hpp"
#include "reboot/os_windows/platform/windows_prerequisites.hpp"
#include "reboot/os_windows/platform/windows_resolver.hpp"
#include "reboot/os_windows/platform/windows_shell.hpp"
#include "reboot/os_windows/platform/windows_system_info.hpp"
#include "reboot/os_windows/platform/wmi_security_product_probe.hpp"
#include "wide.hpp"

namespace reboot::ports {

namespace {

namespace win = os_windows::platform;

[[nodiscard]] std::optional<std::string> launcher_home() {
    const DWORD length = GetEnvironmentVariableW(L"REBOOT_LAUNCHER_HOME", nullptr, 0);
    if (length == 0) return std::nullopt;
    std::wstring value(length, L'\0');
    const DWORD written = GetEnvironmentVariableW(L"REBOOT_LAUNCHER_HOME", value.data(), length);
    value.resize(written);
    return win::narrow(value);
}

}  // namespace

Result<PlatformServices> make_platform(const PlatformOptions& options) {
    auto paths = win::WindowsPaths::detect();
    if (!paths) return std::unexpected(std::move(paths.error()));
    Result<DataRoot> root = options.data_root_override ? Result<DataRoot>(DataRoot{*options.data_root_override, true})
                                                       : resolve_data_root(*paths, launcher_home());
    if (!root) return std::unexpected(std::move(root.error()));
    const std::string hash = root_hash16(canonical_root(*root));

    auto sid = win::UserSid::current();
    if (!sid) return std::unexpected(std::move(sid.error()));
    auto sid_text = sid->to_string();
    if (!sid_text) return std::unexpected(std::move(sid_text.error()));

    auto watcher = win::WindowsFileWatcher::create();
    if (!watcher) return std::unexpected(std::move(watcher.error()));

    PlatformServices services;
    const std::optional<NativePath> velopack_root = paths->velopack_package_dir();
    const NativePath install_root = velopack_root ? *velopack_root : paths->exe_dir();
    const NativePath state_dir = root->root / "state";

    services.fs = std::make_unique<win::WindowsFileSystem>();
    services.logs = std::make_unique<win::WindowsLogFileSystem>();
    services.watcher = std::make_unique<win::WindowsFileWatcher>(std::move(*watcher));
    services.disk = std::make_unique<win::WindowsDiskInfo>();
    services.secrets = std::make_unique<win::CredentialManagerStore>(hash, state_dir / "secrets", *services.fs);
    services.processes = std::make_unique<win::JobProcessLauncher>();
    services.session_host = std::make_unique<win::Win32SessionHost>(*services.fs);
    services.ports = std::make_unique<win::WindowsPortInspector>();
    services.peer_inspector = std::make_unique<win::UnsupportedPeerInspector>();
    services.resolver = std::make_unique<win::WindowsResolver>();
    services.shell = std::make_unique<win::WindowsShell>();
    services.integration = std::make_unique<win::WindowsIntegrationRegistrar>(install_root, std::move(*sid_text));
    services.security = std::make_unique<win::WmiSecurityProductProbe>();
    services.prerequisites = std::make_unique<win::WindowsPrerequisites>();
    services.system = std::make_unique<win::WindowsSystemInfo>();
    services.updater = std::make_unique<win::VelopackApplier>(velopack_root, state_dir / "update-feed");
    services.random = std::make_unique<OsRandom>();
    services.paths = std::make_unique<win::WindowsPaths>(std::move(*paths));
    return services;
}

}  // namespace reboot::ports
