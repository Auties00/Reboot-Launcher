#pragma once

#include <memory>
#include <optional>

#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/native_path.hpp"
#include "reboot/foundation/paths.hpp"
#include "reboot/foundation/random.hpp"
#include "reboot/ports/file_system.hpp"
#include "reboot/ports/ipc.hpp"
#include "reboot/ports/log_file_system.hpp"
#include "reboot/ports/net.hpp"
#include "reboot/ports/os_services.hpp"
#include "reboot/ports/platform_paths.hpp"
#include "reboot/ports/process.hpp"
#include "reboot/ports/runner.hpp"
#include "reboot/ports/secret_store.hpp"
#include "reboot/ports/session_host.hpp"

namespace reboot::ports {

struct PlatformOptions {
    std::optional<NativePath> data_root_override;
    bool foreground = false;
};

// IHttpTransport and IQuicTransport are not here: the engine builds them from the net package.
struct PlatformServices {
    std::unique_ptr<IPlatformPaths> paths;
    std::unique_ptr<IFileSystem> fs;
    // The logger writer's files under the logs directory.
    std::unique_ptr<ILogFileSystem> logs;
    std::unique_ptr<IFileWatcher> watcher;
    std::unique_ptr<IDiskInfo> disk;
    std::unique_ptr<ISecretStore> secrets;
    std::unique_ptr<IProcessLauncher> processes;
    // Windows only; macOS and Linux build compat::WineSessionHost over `runner`.
    std::unique_ptr<ISessionHost> session_host;
    // macOS and Linux only.
    std::unique_ptr<IRunnerPlatform> runner;
    std::unique_ptr<IPortInspector> ports;
    std::unique_ptr<ILoopbackPeerInspector> peer_inspector;
    std::unique_ptr<IResolver> resolver;
    std::unique_ptr<IIpcListener> ipc_listener;
    std::unique_ptr<IShellLauncher> shell;
    std::unique_ptr<IIntegrationRegistrar> integration;
    std::unique_ptr<ISecurityProductProbe> security;
    std::unique_ptr<IPrerequisiteProbe> prerequisites;
    std::unique_ptr<ISystemInfo> system;
    std::unique_ptr<IUpdateApplier> updater;
    std::unique_ptr<IRandom> random;
};

// Defined by core-<os>/platform for the engine.
[[nodiscard]] Result<PlatformServices> make_platform(const PlatformOptions& options);

// The engine's end of its endpoint, which make_platform leaves to the OS ipc module.
struct EngineEndpoint {
    std::unique_ptr<IIpcListener> listener;
    // The user the endpoint is named after, and this process.
    PeerIdentity self;
};

// Defined by core-<os>/ipc for the engine.
[[nodiscard]] Result<EngineEndpoint> make_engine_endpoint();

// Defined by core-<os>/runner where play runs under Wine (macOS and Linux); Windows plays natively
// and has none. `setup_base` is EnvBuilder's daemon-base layer for the runner's own setup runs.
[[nodiscard]] std::unique_ptr<IRunnerPlatform> make_runner_platform(const AppLayout& layout, IProcessLauncher& processes,
                                                                    EnvBlock setup_base);

struct ClientPlatform {
    std::unique_ptr<IIpcConnector> connector;
    std::unique_ptr<IEngineStarter> starter;
    std::unique_ptr<ICallerContextProbe> caller;
    std::unique_ptr<IPlatformPaths> paths;
    // Reads the update marker.
    std::unique_ptr<IFileRevisionReader> revisions;
    // The calling user and process; the engine endpoint is named after the user.
    PeerIdentity self;
};

// Defined by core-<os>/ipc for reboot_client.
[[nodiscard]] Result<ClientPlatform> make_client_platform();

}  // namespace reboot::ports
