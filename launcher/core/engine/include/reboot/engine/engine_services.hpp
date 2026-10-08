#pragma once

#include <memory>
#include <optional>
#include <vector>

#include "reboot/engine/engine_lifecycle.hpp"
#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/function.hpp"
#include "reboot/foundation/paths.hpp"
#include "reboot/process/orphan_reaper.hpp"
#include "reboot/storage/load_report.hpp"
#include "reboot/updates/update_service.hpp"

namespace boost::asio {
class io_context;
}

namespace reboot {
class EventBus;
class Executor;
class IClock;
class OpRegistry;
class Subscription;
class TimerService;
class UserRequestRegistry;
class WorkerPool;
}  // namespace reboot

namespace reboot::ports {
struct PlatformServices;
class IHttpTransport;
class IQuicTransport;
class ISessionHost;
}  // namespace reboot::ports

namespace reboot::logging {
class ErrorRouter;
class LogExporter;
class LogLineForwarder;
class LogRing;
}  // namespace reboot::logging

namespace reboot::trust {
class KeyRing;
class SerialGuard;
}  // namespace reboot::trust

namespace reboot::storage {
class FrontendStateStore;
class ResetService;
class Settings;
class SettingsRegistry;
}  // namespace reboot::storage

namespace reboot::ux {
class AppLinks;
class NoticeService;
class Onboarding;
class SettingsSearch;
}  // namespace reboot::ux

namespace reboot::net {
class AddressResolver;
class HostTlsMemory;
class HttpClient;
class IPortMappingGateway;
class PortMapperService;
class PortOwnerService;
class PortPreflight;
class ResumableDownloader;
class UdpBeaconProber;
}  // namespace reboot::net

namespace reboot::components {
class ComponentStore;
class ManifestService;
}  // namespace reboot::components

namespace reboot::catalog {
class BundledCatalogSource;
class CatalogService;
class SignedRemoteCatalogSource;
}  // namespace reboot::catalog

namespace reboot::support {
class SupportPolicy;
}

namespace reboot::builds {
class BuildInstaller;
class ClTable;
class IArchiveExtractor;
class ImportService;
class Library;
}  // namespace reboot::builds

namespace reboot::secrets {
class SecretService;
}

namespace reboot::identity {
class IdentityService;
}

namespace reboot::game_channel {
class GameChannelListener;
class TokenRegistry;
}  // namespace reboot::game_channel

namespace reboot::compat {
class PrefixManager;
class RuntimeService;
class WineSessionHost;
}  // namespace reboot::compat

namespace reboot::backend {
class BackendAccounts;
class BackendProcess;
class BackendService;
class LegacyFixedArbiter;
class RemoteBackendProbe;
class RemoteLogin;
}  // namespace reboot::backend

namespace reboot::front {
class LegacyFixedListeners;
class SessionFront;
class UpstreamPolicy;
}  // namespace reboot::front

namespace reboot::gameserver {
class GameServerBinary;
}

namespace reboot::sessions {
class SessionRegistry;
class ShutdownCoordinator;
}  // namespace reboot::sessions

namespace reboot::browser {
class BrowserSession;
class DeepLinkService;
class GameServerTarget;
class JoinService;
}  // namespace reboot::browser

namespace reboot::publish {
class HostIdentityStore;
class HostPublisher;
}  // namespace reboot::publish

namespace reboot::host {
class HostPortAllocator;
class HostProfileStore;
class HostService;
}  // namespace reboot::host

namespace reboot::play {
class MatchTargets;
class PlayService;
}  // namespace reboot::play

namespace reboot::integration {
class IntegrationService;
class PrerequisiteService;
class PurgeService;
class ShellService;
}  // namespace reboot::integration

namespace reboot::ipc {
class IpcServer;
}

namespace reboot::engine {

class ApiRouter;
class EngineActivityProbe;
class EngineHostBackendLink;
class EngineStores;
class HostIdentityOwnServers;
class RuntimeRecorder;
class StateGuidanceStore;
struct EngineInfo;

// The process-level pieces EngineHost owns and lends to every service.
struct EngineRuntime {
    ports::PlatformServices& platform;
    ports::IHttpTransport& http;
    ports::IQuicTransport& quic;
    IClock& clock;
    Executor& strand;
    TimerService& timers;
    WorkerPool& workers;
    boost::asio::io_context& io;
    // A Logger sink; Logs.read and LogLine come from it.
    logging::LogRing& log_ring;
    const AppLayout& layout;
    const InstallLayout& install;
    EngineInfo& info;
    // Runs on the strand once the shutdown steps finished.
    UniqueFunction<void(EngineExit)> on_exit;
};

// Capabilities: none. The composition root; steps 4 to 8 run before the strand loop, 9 to 11 on it.
class EngineServices {
public:
    explicit EngineServices(EngineRuntime runtime);
    ~EngineServices();
    EngineServices(const EngineServices&) = delete;
    EngineServices& operator=(const EngineServices&) = delete;

    // Step 4. `memory_only` is set when the data root could not be created.
    [[nodiscard]] std::vector<storage::LoadReport> open_stores(std::optional<Diagnostic> memory_only);
    // Step 5.
    process::OrphanReapReport reap_orphans();
    // Step 6.
    void load_components_and_catalog();
    // Step 7: only when state.json's last_run_version differs from this build.
    void reconcile_integration();
    // Step 8: only while state/engine.lock is held.
    Result<void> open_endpoint();
    // The pending-update marker and state/resume.json.
    [[nodiscard]] Result<updates::StartupResume> begin_resume();

    // Step 9.
    void start();
    // Step 10: relaunches the recorded hosts. A pending update's loopback self-test runs on the
    // WorkerPool, since only a running strand answers its Hello.
    void apply_resume(const updates::StartupResume& resume, Result<void> self_test);
    // Step 11.
    Result<void> write_runtime();

    void on_os_signal();

private:
    // The platform's, else wine_session_host_.
    [[nodiscard]] ports::ISessionHost& play_session_host() noexcept;

    EngineRuntime runtime_;

    std::unique_ptr<EventBus> events_;
    std::unique_ptr<OpRegistry> ops_;
    std::unique_ptr<UserRequestRegistry> requests_;
    std::unique_ptr<EngineStores> stores_;
    std::unique_ptr<RuntimeRecorder> runtime_recorder_;

    std::unique_ptr<logging::LogLineForwarder> log_forwarder_;
    std::unique_ptr<logging::LogExporter> log_exporter_;
    std::unique_ptr<logging::ErrorRouter> error_router_;

    std::unique_ptr<trust::KeyRing> manifest_keys_;
    std::unique_ptr<trust::KeyRing> catalog_keys_;
    std::unique_ptr<trust::SerialGuard> serial_guard_;

    std::unique_ptr<storage::SettingsRegistry> settings_registry_;
    std::unique_ptr<storage::Settings> settings_;
    std::unique_ptr<storage::FrontendStateStore> frontend_state_;

    std::unique_ptr<net::HostTlsMemory> tls_memory_;
    std::unique_ptr<net::HttpClient> http_;
    std::unique_ptr<net::ResumableDownloader> downloader_;
    std::unique_ptr<net::AddressResolver> resolver_;
    std::unique_ptr<net::PortOwnerService> port_owners_;
    std::unique_ptr<net::PortPreflight> port_preflight_;
    std::unique_ptr<net::IPortMappingGateway> upnp_;
    std::unique_ptr<net::IPortMappingGateway> natpmp_;
    std::unique_ptr<net::PortMapperService> port_mapper_;
    std::unique_ptr<net::UdpBeaconProber> beacon_prober_;

    std::unique_ptr<components::ManifestService> manifest_;
    std::unique_ptr<components::ComponentStore> components_;
    std::unique_ptr<catalog::SignedRemoteCatalogSource> remote_catalog_;
    std::unique_ptr<catalog::BundledCatalogSource> bundled_catalog_;
    std::unique_ptr<catalog::CatalogService> catalog_;
    std::unique_ptr<support::SupportPolicy> support_;

    std::unique_ptr<builds::ClTable> cl_table_;
    std::unique_ptr<builds::IArchiveExtractor> extractor_;
    std::unique_ptr<builds::Library> library_;
    std::unique_ptr<builds::BuildInstaller> installer_;
    std::unique_ptr<builds::ImportService> build_import_;

    std::unique_ptr<secrets::SecretService> secrets_;
    std::unique_ptr<identity::IdentityService> identity_;

    std::unique_ptr<game_channel::TokenRegistry> channel_tokens_;
    std::unique_ptr<game_channel::GameChannelListener> game_channel_;
    std::unique_ptr<compat::PrefixManager> prefixes_;
    std::unique_ptr<compat::RuntimeService> runtimes_;
    // macOS and Linux only.
    std::unique_ptr<compat::WineSessionHost> wine_session_host_;

    std::unique_ptr<backend::BackendProcess> backend_process_;
    std::unique_ptr<backend::RemoteBackendProbe> backend_probe_;
    std::unique_ptr<backend::BackendService> backend_;
    std::unique_ptr<backend::BackendAccounts> backend_accounts_;
    std::unique_ptr<backend::RemoteLogin> remote_login_;
    std::unique_ptr<backend::LegacyFixedArbiter> legacy_fixed_arbiter_;

    std::unique_ptr<front::UpstreamPolicy> upstream_policy_;
    std::unique_ptr<front::SessionFront> front_;
    std::unique_ptr<front::LegacyFixedListeners> legacy_listeners_;

    std::unique_ptr<gameserver::GameServerBinary> game_server_binary_;

    std::unique_ptr<sessions::SessionRegistry> sessions_;
    std::unique_ptr<sessions::ShutdownCoordinator> shutdown_;
    std::unique_ptr<storage::ResetService> reset_;

    std::unique_ptr<publish::HostIdentityStore> host_identities_;
    std::unique_ptr<publish::HostPublisher> publisher_;

    std::unique_ptr<browser::BrowserSession> browser_;
    std::unique_ptr<HostIdentityOwnServers> own_servers_;
    std::unique_ptr<browser::JoinService> join_;
    std::unique_ptr<browser::GameServerTarget> addresses_;
    std::unique_ptr<browser::DeepLinkService> deep_links_;

    std::unique_ptr<host::HostProfileStore> host_profiles_;
    std::unique_ptr<host::HostPortAllocator> port_allocator_;
    std::unique_ptr<EngineHostBackendLink> host_backend_link_;
    std::unique_ptr<host::HostService> hosts_;

    std::unique_ptr<play::MatchTargets> match_targets_;
    std::unique_ptr<play::PlayService> play_;

    std::unique_ptr<StateGuidanceStore> guidance_store_;
    std::unique_ptr<ux::NoticeService> notices_;
    std::unique_ptr<ux::Onboarding> onboarding_;
    std::unique_ptr<ux::SettingsSearch> settings_search_;
    std::unique_ptr<ux::AppLinks> app_links_;

    std::unique_ptr<integration::IntegrationService> integration_;
    std::unique_ptr<integration::PrerequisiteService> prerequisites_;
    std::unique_ptr<integration::PurgeService> purge_;
    std::unique_ptr<integration::ShellService> shell_;

    std::unique_ptr<EngineActivityProbe> activity_;
    std::unique_ptr<EngineLifecycle> lifecycle_;
    std::unique_ptr<updates::UpdateService> updates_;
    std::unique_ptr<ApiRouter> router_;
    std::unique_ptr<ipc::IpcServer> ipc_;
    // ForegroundHint is no API event, so it goes to IpcServer::send_foreground_hint.
    std::shared_ptr<Subscription> foreground_hints_;
};

}  // namespace reboot::engine
