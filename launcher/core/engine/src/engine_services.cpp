#include "reboot/engine/engine_services.hpp"

#include <algorithm>
#include <cstdlib>
#include <limits>
#include <string>
#include <utility>
#include <variant>

#include "api_convert.hpp"
#include "engine_adapters.hpp"
#include "engine_host_backend_link.hpp"
#include "engine_state_json.hpp"
#include "host_identity_own_servers.hpp"
#include "host_platform.hpp"
#include "messages.hpp"
#include "reboot/api/v1/guidance.hpp"
#include "reboot/backend/backend_accounts.hpp"
#include "reboot/backend/backend_config.hpp"
#include "reboot/backend/backend_launch.hpp"
#include "reboot/backend/backend_process.hpp"
#include "reboot/backend/backend_service.hpp"
#include "reboot/backend/remote_backend_probe.hpp"
#include "reboot/backend/remote_login.hpp"
#include "reboot/browser/browser_session.hpp"
#include "reboot/browser/deep_link_service.hpp"
#include "reboot/browser/game_server_target.hpp"
#include "reboot/browser/join_service.hpp"
#include "reboot/browser/rbsb_endpoint.hpp"
#include "reboot/browser/server_list.hpp"
#include "reboot/builds/build_installer.hpp"
#include "reboot/builds/cl_table.hpp"
#include "reboot/builds/import_service.hpp"
#include "reboot/builds/libarchive_extractor.hpp"
#include "reboot/builds/library.hpp"
#include "reboot/catalog/bundled_catalog_source.hpp"
#include "reboot/catalog/catalog_service.hpp"
#include "reboot/catalog/signed_remote_catalog_source.hpp"
#include "reboot/compat/prefix_manager.hpp"
#include "reboot/compat/runtime_service.hpp"
#include "reboot/compat/wine_session_host.hpp"
#include "reboot/components/component_store.hpp"
#include "reboot/components/manifest_platform.hpp"
#include "reboot/components/manifest_service.hpp"
#include "reboot/contracts/ipc.hpp"
#include "reboot/engine/api_router.hpp"
#include "reboot/engine/engine_activity_probe.hpp"
#include "reboot/engine/engine_info.hpp"
#include "reboot/engine/engine_stores.hpp"
#include "reboot/foundation/events.hpp"
#include "reboot/foundation/executor.hpp"
#include "reboot/foundation/log.hpp"
#include "reboot/foundation/operation.hpp"
#include "reboot/foundation/user_request.hpp"
#include "reboot/front/legacy_fixed_listeners.hpp"
#include "reboot/front/session_front.hpp"
#include "reboot/game_channel/game_channel_listener.hpp"
#include "reboot/game_channel/token_registry.hpp"
#include "reboot/gameserver/game_server_binary.hpp"
#include "reboot/host/host_port_allocator.hpp"
#include "reboot/host/host_profile_store.hpp"
#include "reboot/host/host_service.hpp"
#include "reboot/host/host_start_request.hpp"
#include "reboot/identity/identity_changed_event.hpp"
#include "reboot/identity/identity_service.hpp"
#include "reboot/integration/integration_service.hpp"
#include "reboot/integration/integration_targets.hpp"
#include "reboot/integration/prerequisite_service.hpp"
#include "reboot/integration/purge_service.hpp"
#include "reboot/integration/purge_targets.hpp"
#include "reboot/integration/shell_service.hpp"
#include "reboot/ipc/ipc_server.hpp"
#include "reboot/logging/error_router.hpp"
#include "reboot/logging/file_log_sink.hpp"
#include "reboot/logging/log_exporter.hpp"
#include "reboot/logging/log_line_forwarder.hpp"
#include "reboot/logging/log_ring.hpp"
#include "reboot/net/address_resolver.hpp"
#include "reboot/net/host_tls_memory.hpp"
#include "reboot/net/http_client.hpp"
#include "reboot/net/port_mapper_service.hpp"
#include "reboot/net/port_owner_service.hpp"
#include "reboot/net/port_preflight.hpp"
#include "reboot/net/resumable_downloader.hpp"
#include "reboot/net/udp_beacon_prober.hpp"
#include "reboot/play/match_targets.hpp"
#include "reboot/play/play_service.hpp"
#include "reboot/ports/platform_services.hpp"
#include "reboot/process/env_builder.hpp"
#include "reboot/process/orphan_reaper.hpp"
#include "reboot/publish/host_identity_store.hpp"
#include "reboot/publish/host_publisher.hpp"
#include "reboot/secrets/secret_error.hpp"
#include "reboot/secrets/secret_service.hpp"
#include "reboot/sessions/session_info.hpp"
#include "reboot/sessions/session_registry.hpp"
#include "reboot/sessions/shutdown_coordinator.hpp"
#include "reboot/sessions/stop_request.hpp"
#include "reboot/storage/frontend_state_store.hpp"
#include "reboot/storage/reset_service.hpp"
#include "reboot/storage/settings.hpp"
#include "reboot/storage/settings_changed.hpp"
#include "reboot/storage/settings_keys.hpp"
#include "reboot/storage/settings_registry.hpp"
#include "reboot/support/support_inputs.hpp"
#include "reboot/support/support_policy.hpp"
#include "reboot/trust/serial_guard.hpp"
#include "reboot/updates/update_offer.hpp"
#include "reboot/ux/app_links.hpp"
#include "reboot/ux/notice_service.hpp"
#include "reboot/ux/onboarding.hpp"
#include "reboot/ux/settings_search.hpp"
#include "runtime_recorder.hpp"
#include "state_extras.hpp"
#include "state_guidance_store.hpp"

namespace rb::engine {

namespace {

// The release assets the signed release manifest and build catalog are published as; each has its
// detached ".sig" beside it.
constexpr std::string_view kManifestUrl = "https://github.com/Auties00/Reboot-Launcher/releases/latest/download/manifest.json";
constexpr std::string_view kCatalogUrl = "https://github.com/Auties00/Reboot-Launcher/releases/latest/download/catalog.json";
// A host other than the edge's, so a failed browse connect tells an offline machine from a down edge.
constexpr std::string_view kConnectivityUrl = "https://github.com/";
constexpr std::string_view kDefaultServerName = "Reboot Server";
constexpr std::size_t kReactionBudget = std::size_t{1} << 20;
constexpr std::size_t kForegroundBudget = std::size_t{64} << 10;

[[nodiscard]] constexpr process::EnvSyntax env_syntax() noexcept {
    return kWindowsHost ? process::EnvSyntax::Windows : process::EnvSyntax::Posix;
}

void log_failure(std::string_view what, const Result<void>& result) {
    if (!result) REBOOT_LOG_WARN(Engine, "{} failed: {}", what, result.error().id);
}

[[nodiscard]] std::optional<browser::RbsbExpertOverride> rbsb_expert_override() {
    const std::optional<std::string> endpoint = own_environment(browser::kRbsbEndpointEnv.data());
    if (!endpoint) return std::nullopt;
    std::optional<NativePath> ca;
    if (const std::optional<std::string> bundle = own_environment(browser::kRbsbCaEnv.data()))
        ca = NativePath(std::u8string(bundle->begin(), bundle->end()));
    Result<browser::RbsbExpertOverride> parsed = browser::RbsbExpertOverride::parse(*endpoint, std::move(ca));
    if (!parsed) {
        REBOOT_LOG_WARN(Browser, "{} is ignored: {}", browser::kRbsbEndpointEnv, parsed.error().id);
        return std::nullopt;
    }
    return *parsed;
}

[[nodiscard]] std::string signature_url(std::string_view url) { return std::string(url) + ".sig"; }

}  // namespace

EngineServices::EngineServices(EngineRuntime runtime) : runtime_(std::move(runtime)) {
    events_ = std::make_unique<EventBus>(runtime_.info.epoch);
    ops_ = std::make_unique<OpRegistry>(runtime_.clock, runtime_.timers, *events_);
    requests_ = std::make_unique<UserRequestRegistry>(*events_);
    stores_ = std::make_unique<EngineStores>(*runtime_.platform.fs, runtime_.workers, runtime_.strand, runtime_.clock,
                                             runtime_.layout);
    runtime_recorder_ = std::make_unique<RuntimeRecorder>(stores_->runtime);
}

EngineServices::~EngineServices() {
    if (reactions_) reactions_->set_notify({});
    if (foreground_hints_) foreground_hints_->set_notify({});
    if (ops_) ops_->set_outcome_hook({});
    // IPC and the router go first, so no client call reaches a service being torn down.
    ipc_.reset();
    router_.reset();
}

std::vector<storage::LoadReport> EngineServices::open_stores(std::optional<Diagnostic> memory_only) {
    std::vector<storage::LoadReport> reports = stores_->load(std::move(memory_only));
    runtime_.info.storage_mode = stores_->mode();
    for (const storage::LoadReport& report : reports)
        if (report.reason)
            REBOOT_LOG_WARN(Storage, "{} opened {}: {}", report.document,
                            report.mode == storage::StorageMode::ReadWrite ? "read-write" : "restricted", report.reason->id);
    build_services();
    return reports;
}

process::OrphanReapReport EngineServices::reap_orphans() {
    WaitingExecutor completions;
    process::OrphanReaper reaper(*runtime_.platform.processes, runtime_.workers, completions);
    std::optional<Result<process::OrphanReapReport>> outcome;
    reaper.reap(runtime_recorder_->recorded_children(), CancelToken{},
                [&outcome](Result<process::OrphanReapReport> report) { outcome = std::move(report); });
    completions.run_until([&outcome] { return outcome.has_value(); });
    if (!*outcome) {
        REBOOT_LOG_ERROR(Engine, "reaping the previous engine's children failed: {}", outcome->error().id);
        return {};
    }
    process::OrphanReapReport report = std::move(**outcome);
    std::vector<process::ChildRecord> settled = report.killed;
    settled.insert(settled.end(), report.gone.begin(), report.gone.end());
    log_failure("forgetting reaped children", runtime_recorder_->forget(settled));
    for (const process::ChildRecord& child : report.killed)
        REBOOT_LOG_INFO(Engine, "ended pid {} that a previous engine left running", child.pid);
    for (const auto& [child, error] : report.failed)
        REBOOT_LOG_WARN(Engine, "pid {} that a previous engine left could not be ended: {}", child.pid, error.id);
    return report;
}

void EngineServices::load_components_and_catalog() {
    manifest_->load([](Result<components::ManifestOrigin> loaded) {
        if (!loaded) REBOOT_LOG_WARN(Update, "no release manifest is in use: {}", loaded.error().id);
    });
    components_->load([](Result<void> loaded) { log_failure("loading the component store", loaded); });
    if (Result<OpHandle> refresh = catalog_->start_refresh(catalog::CatalogRefresh::IfExpired, DisconnectPolicy::Detached); !refresh)
        REBOOT_LOG_WARN(Builds, "the build catalog was not refreshed: {}", refresh.error().id);
}

void EngineServices::reconcile_integration() {
    if (stores_->state.get().last_run_version == runtime_.info.app_version) return;
    integration_->reconcile(runtime_.info.app_version, CancelToken{},
                            [](std::optional<integration::ReconcileReport> report) {
                                if (report && !report->written.empty())
                                    REBOOT_LOG_INFO(Engine, "OS integration rewrote {} entries", report->written.size());
                            });
}

Result<void> EngineServices::open_endpoint() {
    if (!runtime_.platform.ipc_listener)
        return make_diag(ErrorDomain::Engine, msg::kEndpointFailed).arg("endpoint", runtime_.info.endpoint).fail();
    ipc_ = std::make_unique<ipc::IpcServer>(
        ipc::IpcServerDeps{*runtime_.platform.ipc_listener, runtime_.strand, runtime_.clock, runtime_.timers, *ops_,
                           *events_, *router_},
        to_engine_hello(runtime_.info));
    if (Result<void> started = ipc_->start(runtime_.info.endpoint); !started)
        return make_diag(ErrorDomain::Engine, msg::kEndpointFailed)
            .arg("endpoint", runtime_.info.endpoint)
            .cause(std::move(started.error()))
            .fail();
    foreground_hints_ = events_->subscribe(EventFilter{{EventKind::ForegroundHint}, std::nullopt, std::nullopt}, kForegroundBudget);
    foreground_hints_->set_notify([this] {
        std::vector<EventEnvelope> hints;
        foreground_hints_->drain(hints, std::numeric_limits<std::size_t>::max());
        foreground_hints_->take_resync();
        for (const EventEnvelope& hint : hints)
            if (const auto* payload = std::any_cast<contracts::ipc::ForegroundHint>(&hint.payload))
                ipc_->send_foreground_hint(payload->pid);
    });
    return {};
}

Result<updates::StartupResume> EngineServices::begin_resume() { return updates_->begin_startup(); }

ports::IRunnerPlatform& EngineServices::runner_platform() noexcept {
    return runtime_.platform.runner ? *runtime_.platform.runner : *native_runner_;
}

ports::ISessionHost& EngineServices::play_session_host() noexcept {
    return runtime_.platform.session_host ? *runtime_.platform.session_host
                                          : static_cast<ports::ISessionHost&>(*wine_session_host_);
}

void EngineServices::build_services() {
    ports::PlatformServices& platform = runtime_.platform;
    Executor& strand = runtime_.strand;
    WorkerPool& workers = runtime_.workers;
    TimerService& timers = runtime_.timers;
    IClock& clock = runtime_.clock;
    const AppLayout& layout = runtime_.layout;
    const InstallLayout& install = runtime_.install;
    EngineInfo& info = runtime_.info;
    Redactor& redactor = Logger::redactor();
    ports::IFileSystem& fs = *platform.fs;
    IRandom& random = *platform.random;
    const storage::SettingsValues settings_now = [&] {
        const storage::SettingsDocument& document = stores_->settings.get();
        return document.values;
    }();

    // Logs.
    log_forwarder_ = std::make_unique<logging::LogLineForwarder>(runtime_.log_ring, *events_, strand);
    error_router_ = std::make_unique<logging::ErrorRouter>(strand, clock);
    if (runtime_.session_log != nullptr) error_router_->watch(*runtime_.session_log);
    ops_->set_outcome_hook([errors = error_router_.get()](OpId op, OpKind kind, const std::optional<SessionId>& session,
                                                          ErasedOutcome& outcome) {
        errors->record_outcome(op, kind, session, outcome);
    });
    log_exporter_ = std::make_unique<logging::LogExporter>(layout.logs_dir(), redactor, *ops_, workers, strand);

    // Trust.
    manifest_keys_ = std::make_unique<trust::KeyRing>(
        runtime_.overrides.manifest_keys
            ? trust::KeyRing(trust::SignedDocumentKind::ReleaseManifest, *runtime_.overrides.manifest_keys)
            : trust::KeyRing::pinned(trust::SignedDocumentKind::ReleaseManifest));
    catalog_keys_ = std::make_unique<trust::KeyRing>(
        runtime_.overrides.catalog_keys ? trust::KeyRing(trust::SignedDocumentKind::BuildCatalog, *runtime_.overrides.catalog_keys)
                                        : trust::KeyRing::pinned(trust::SignedDocumentKind::BuildCatalog));
    const SerialFloors floors = serials_from_json(state_extra(stores_->state.get(), kSerialsKey));
    auto persist_floor = [this](bool catalog) {
        return [this, catalog](u64 serial) -> Result<void> {
            SerialFloors next = serials_from_json(state_extra(stores_->state.get(), kSerialsKey));
            (catalog ? next.catalog : next.manifest) = serial;
            return put_state_extra(stores_->state, kSerialsKey, serials_to_json(next));
        };
    };
    manifest_serials_ = std::make_unique<trust::SerialGuard>(trust::SignedDocumentKind::ReleaseManifest, floors.manifest,
                                                             persist_floor(false));
    catalog_serials_ =
        std::make_unique<trust::SerialGuard>(trust::SignedDocumentKind::BuildCatalog, floors.catalog, persist_floor(true));

    // Settings.
    settings_registry_ = std::make_unique<storage::SettingsRegistry>();
    settings_ = std::make_unique<storage::Settings>(stores_->settings, *settings_registry_, *events_);
    frontend_state_ = std::make_unique<storage::FrontendStateStore>(fs, workers, strand, layout.frontend_dir(), stores_->mode());

    // Network.
    tls_memory_ = std::make_unique<net::HostTlsMemory>(
        stores_->state.get().upstream_tls, [this](std::vector<storage::UpstreamTlsMemory> records) {
            Result<u64> written = stores_->state.update([records = std::move(records)](storage::StateDocument& document) mutable {
                document.upstream_tls = std::move(records);
            });
            if (!written) REBOOT_LOG_WARN(Net, "the upstream TLS memory was not saved: {}", written.error().id);
        });
    http_ = std::make_unique<net::HttpClient>(runtime_.http, *tls_memory_, strand, timers, random);
    downloader_ = std::make_unique<net::ResumableDownloader>(*http_, *platform.disk, fs, workers, strand, timers, clock);
    resolver_ = std::make_unique<net::AddressResolver>(*platform.resolver, strand, timers);
    port_owners_ = std::make_unique<net::PortOwnerService>(*platform.ports, *platform.processes);
    port_preflight_ = std::make_unique<net::PortPreflight>(runtime_.io, *port_owners_);
    upnp_ = runtime_.overrides.upnp ? std::move(runtime_.overrides.upnp) : net::make_miniupnpc_gateway();
    natpmp_ = runtime_.overrides.natpmp ? std::move(runtime_.overrides.natpmp) : net::make_natpmp_gateway();
    port_mapper_ = std::make_unique<net::PortMapperService>(
        *upnp_, *natpmp_, info.root_hash16, mapping_records_from_json(state_extra(stores_->state.get(), kPortMappingsKey)),
        [this](std::vector<net::MappingRecord> records) {
            log_failure("saving the port mapping marker",
                        put_state_extra(stores_->state, kPortMappingsKey, mapping_records_to_json(records)));
        },
        workers, strand, timers, *events_);
    datagrams_ = runtime_.overrides.datagrams ? std::move(runtime_.overrides.datagrams)
                                              : net::make_asio_datagram_connector(runtime_.io);
    beacon_prober_ = std::make_unique<net::UdpBeaconProber>(*datagrams_, strand, timers, clock);

    // Components, catalog and support.
    components::ManifestOptions manifest_options;
    manifest_options.url = std::string(kManifestUrl);
    manifest_options.signature_url = signature_url(kManifestUrl);
    manifest_options.channel = std::string(updates::manifest_channel(settings_now.updates.channel));
    manifest_ = std::make_unique<components::ManifestService>(
        components::ManifestServiceDeps{fs, *http_, workers, strand, clock, *manifest_keys_, *manifest_serials_},
        std::move(manifest_options), layout, install);
    components_ = std::make_unique<components::ComponentStore>(
        components::ComponentStoreDeps{fs, *platform.watcher, *platform.security, *downloader_, workers, strand, *ops_,
                                       *events_, *manifest_},
        layout);
    remote_catalog_ = std::make_unique<catalog::SignedRemoteCatalogSource>(
        catalog::RemoteCatalogLocation{std::string(kCatalogUrl), signature_url(kCatalogUrl)}, layout, *http_, fs, workers,
        strand, *catalog_keys_, *catalog_serials_);
    bundled_catalog_ = std::make_unique<catalog::BundledCatalogSource>(install, fs, workers, strand, *catalog_keys_);
    catalog_ = std::make_unique<catalog::CatalogService>(*remote_catalog_, *bundled_catalog_, *ops_, *events_, clock);
    support_ = std::make_unique<support::SupportPolicy>(support::SupportInputs{});

    // Sessions, then what uses them.
    sessions_ = std::make_unique<sessions::SessionRegistry>(clock, random, strand, timers, *events_);
    shutdown_ = std::make_unique<sessions::ShutdownCoordinator>(clock, timers, strand);

    cl_table_ = std::make_unique<builds::ClTable>();
    extractor_ = std::make_unique<builds::LibArchiveExtractor>();
    library_ = std::make_unique<builds::Library>(builds::LibraryDeps{stores_->library, *sessions_, *cl_table_, *catalog_, fs,
                                                                     *platform.shell, workers, strand, *ops_, *events_,
                                                                     clock, random, install});
    builds::InstallerConfig installer_config;
    installer_config.policy = kWindowsHost ? builds::DestinationPolicy::LargestFittingVolume
                                           : builds::DestinationPolicy::UnderDataRoot;
    installer_config.data_root_builds_dir = layout.root() / "builds";
    installer_ = std::make_unique<builds::BuildInstaller>(
        builds::BuildInstallerDeps{*catalog_, *cl_table_, *library_, *downloader_, *extractor_, *platform.disk, fs, workers,
                                   strand, *ops_},
        installer_config);
    build_import_ = std::make_unique<builds::ImportService>(
        builds::ImportServiceDeps{*library_, *catalog_, *cl_table_, fs, *requests_, workers, strand, *ops_});

    secrets_ = std::make_unique<secrets::SecretService>(*platform.secrets, workers, strand, timers, *requests_, *events_,
                                                        redactor, info.root_hash16);
    identity_ = std::make_unique<identity::IdentityService>(stores_->accounts, stores_->backend_logins, random, *events_);

    // Play's channel and runners.
    channel_tokens_ = std::make_unique<game_channel::TokenRegistry>(random, redactor);
    game_channel_ = std::make_unique<game_channel::GameChannelListener>(runtime_.io, strand, timers, *channel_tokens_);
    if (!platform.runner) native_runner_ = std::make_unique<NativeOnlyRunnerPlatform>();
    prefixes_ = std::make_unique<compat::PrefixManager>(
        compat::PrefixManagerDeps{*platform.processes, runner_platform(), fs, workers, strand, timers, stores_->compat}, layout);
    runtimes_ = std::make_unique<compat::RuntimeService>(compat::RuntimeServiceDeps{
        *components_, *manifest_, runner_platform(), *prefixes_, *requests_, *ops_, workers, strand, clock, stores_->compat});
    if (!platform.session_host)
        wine_session_host_ = std::make_unique<compat::WineSessionHost>(compat::WineSessionHostDeps{
            *platform.processes, runner_platform(), *game_channel_, timers, strand,
            [](SessionId session, std::string_view line) {
                if (Logger::enabled(LogLevel::Info))
                    Logger::write(LogLevel::Info, LogCategory::Wine, session, std::string(line));
            }});

    // Backend.
    backend_sessions_ = std::make_unique<EngineBackendSessions>(*sessions_, strand);
    backend::BackendLaunch launch;
    launch.exe = install.backend_exe;
    launch.content_dir = install.backend_content_dir;
    launch.data_dir = layout.backend_dir();
    launch.user_environment = runtime_.user_environment;
    launch.env_syntax = env_syntax();
    Result<process::ProcessSpec> backend_spec = backend::make_backend_spec(launch);
    if (!backend_spec) REBOOT_LOG_ERROR(Backend, "the embedded backend cannot start: {}", backend_spec.error().id);
    backend_process_ = std::make_unique<backend::BackendProcess>(
        *platform.processes, strand, timers, clock, redactor,
        backend_spec ? std::move(*backend_spec) : process::ProcessSpec{}, runtime_recorder_->recorder(), LogLevel::Info);
    backend_probe_ = std::make_unique<backend::RemoteBackendProbe>(*http_);
    Result<backend::BackendConfig> backend_config = backend::BackendConfig::from_settings(settings_now.backend);
    if (!backend_config)
        REBOOT_LOG_WARN(Backend, "the configured backend is unusable, the embedded one is used: {}", backend_config.error().id);
    backend_ = std::make_unique<backend::BackendService>(backend_config.value_or(backend::BackendConfig{}), *backend_process_,
                                                         *backend_probe_, *backend_sessions_, *ops_, *requests_, *tls_memory_,
                                                         *events_, strand, timers);
    backend_accounts_ = std::make_unique<backend::BackendAccounts>(*backend_, *backend_process_, *ops_, *requests_, *events_);
    remote_login_ = std::make_unique<backend::RemoteLogin>(*http_, *secrets_, redactor);

    // Front.
    front::FrontOptions front_options;
    if constexpr (!kWindowsHost) front_options.ca_bundle = platform.system->ca_bundle();
    front_options.engine_uid = runtime_.uid;
    front_ = std::make_unique<front::SessionFront>(runtime_.io, strand, timers, workers, *tls_memory_, *requests_,
                                                   *platform.peer_inspector, std::move(front_options));
    legacy_listeners_ = std::make_unique<front::LegacyFixedListeners>(runtime_.io, strand, workers, *front_, *port_owners_,
                                                                      *events_);

    // Game server.
    Result<NativePath> game_server_exe = gameserver::locate_game_server(install, std::nullopt);
    game_server_binary_ = std::make_unique<gameserver::GameServerBinary>(
        game_server_exe.value_or(install.game_server_exe), fs, *platform.processes, workers, strand, timers, clock,
        stores_->describe_cache,
        [this]() -> Result<process::BuiltEnv> {
            process::EnvBuilder builder(env_syntax());
            builder.daemon_base(runtime_.user_environment);
            return std::move(builder).build();
        },
        runtime_recorder_->recorder());

    // Host profiles and identities, which the browser needs to recognise our own servers.
    host_profiles_ = std::make_unique<host::HostProfileStore>(stores_->host_profiles, random);
    log_failure("adding the built-in host profiles", host_profiles_->ensure_builtin(settings_now.host.listing));
    host_identities_ = std::make_unique<publish::HostIdentityStore>(fs, workers, strand, random, *ops_,
                                                                    layout.host_identity_dir());
    std::vector<HostProfileId> profile_ids;
    for (const host::HostProfile& profile : host_profiles_->list()) profile_ids.push_back(profile.id);
    if (Result<std::vector<publish::IdentityLoadIssue>> loaded = host_identities_->load(profile_ids); !loaded) {
        REBOOT_LOG_WARN(Host, "host identities live in memory only: {}", loaded.error().id);
        host_identities_->load_memory_only();
    } else {
        for (const publish::IdentityLoadIssue& issue : *loaded) error_router_->report(issue.reason, LogCategory::Host, std::nullopt);
    }
    publish_notices_ = std::make_unique<ErrorRouterPublishNotices>(*error_router_);
    const browser::RbsbEndpoint edge = browser::select_rbsb_endpoint(std::nullopt, rbsb_expert_override());
    publisher_ = std::make_unique<publish::HostPublisher>(runtime_.quic, *host_identities_, *publish_notices_, strand, timers,
                                                          random, *events_, edge);

    // Browser and join.
    browser::BrowserSessionOptions browser_options;
    browser_options.client_version = info.app_version.to_string();
    browser_options.connectivity_url = std::string(kConnectivityUrl);
    browser_ = std::make_unique<browser::BrowserSession>(
        browser::BrowserSessionDeps{runtime_.quic, *resolver_, *http_, strand, timers, clock, random, *events_}, edge,
        std::move(browser_options));
    own_servers_ = std::make_unique<HostIdentityOwnServers>(*host_identities_, *host_profiles_);
    join_ = std::make_unique<browser::JoinService>(
        *browser_, *own_servers_, *requests_, *ops_, clock, [this](RequestId request) -> std::optional<SecretString> {
            Result<secrets::SecretTarget> target = secrets::SecretTarget::parse(
                secrets::SecretKind::JoinPassword, secrets::SecretScope::join_request(request).text());
            if (!target) return std::nullopt;
            Result<SecretBytes> taken = secrets_->take(*target);
            if (!taken) return std::nullopt;
            return SecretString{std::string(taken->reveal().begin(), taken->reveal().end())};
        });
    addresses_ = std::make_unique<browser::GameServerTarget>(
        *resolver_, *beacon_prober_, *ops_, *events_, join_target_from_json(state_extra(stores_->state.get(), kJoinTargetKey)),
        [this](const std::optional<browser::JoinTarget>& target) {
            log_failure("saving the join target", put_state_extra(stores_->state, kJoinTargetKey, join_target_to_json(target)));
        });
    deep_links_ = std::make_unique<browser::DeepLinkService>(*browser_, *addresses_, *own_servers_, *requests_, *ops_);
    server_list_ = std::make_unique<browser::ServerList>(
        *browser_, *own_servers_, clock, *events_, browse_choices_from_json(state_extra(stores_->state.get(), kBrowseChoicesKey)),
        [this](const browser::BrowseChoices& choices) {
            log_failure("saving the browse choices",
                        put_state_extra(stores_->state, kBrowseChoicesKey, browse_choices_to_json(choices)));
        });

    // Hosting.
    port_allocator_ = std::make_unique<host::HostPortAllocator>(*port_preflight_, workers, strand);
    host_backend_link_ = std::make_unique<EngineHostBackendLink>(*backend_, strand, random);
    host::HostServiceOptions host_options;
    host_options.default_server_name = std::string(kDefaultServerName);
    hosts_ = std::make_unique<host::HostService>(host::HostServiceDeps{
        .profiles = *host_profiles_,
        .sessions = *sessions_,
        .binary = *game_server_binary_,
        .support = *support_,
        .library = *library_,
        .identity = *identity_,
        .settings = *settings_,
        .allocator = *port_allocator_,
        .port_owners = *port_owners_,
        .prober = *beacon_prober_,
        .mapper = *port_mapper_,
        .publisher = *publisher_,
        .identities = *host_identities_,
        .backend = *host_backend_link_,
        .launcher = *platform.processes,
        .fs = fs,
        .workers = workers,
        .strand = strand,
        .timers = timers,
        .clock = clock,
        .ops = *ops_,
        .events = *events_,
        .requests = *requests_,
        .layout = layout,
        .join_password = [this](const HostProfileId& profile) -> Result<std::optional<SecretString>> {
            Result<secrets::SecretTarget> target = secrets::SecretTarget::parse(
                secrets::SecretKind::HostJoinPassword, secrets::SecretScope::host_profile(profile).text());
            if (!target) return std::unexpected(std::move(target.error()));
            Result<SecretBytes> value = secrets_->provide(*target);
            if (!value) {
                if (secrets::has_error(value.error(), secrets::SecretError::NotFound)) return std::optional<SecretString>();
                return std::unexpected(std::move(value.error()));
            }
            return std::optional<SecretString>(SecretString{std::string(value->reveal().begin(), value->reveal().end())});
        },
        .base_env = [this]() -> Result<process::BuiltEnv> {
            process::EnvBuilder builder(env_syntax());
            builder.daemon_base(runtime_.user_environment);
            return std::move(builder).build();
        },
        .record = runtime_recorder_->recorder(),
        .options = std::move(host_options)});

    // Play.
    match_targets_ = std::make_unique<play::MatchTargets>();
    backend_process_->set_match_target_resolver(match_targets_.get());
    play::PlayRunner runner = platform.session_host
                                  ? play::PlayRunner{play::NativeRunner{*platform.session_host}}
                                  : play::PlayRunner{play::WineRunner{*runtimes_, *prefixes_, *wine_session_host_}};
    play::PlayServiceOptions play_options;
    play_options.session_match = kLinuxHost ? play::SessionMatch::Display : play::SessionMatch::OsSession;
    play_options.daemon_env = runtime_.user_environment;
    play_options.wine_log_dir = layout.logs_dir();
    play_ = std::make_unique<play::PlayService>(
        play::PlayServiceDeps{*settings_,       *library_,         *catalog_,        *support_,         *components_,
                              std::move(runner), *identity_,       *secrets_,        *backend_,         *remote_login_,
                              *front_,           *legacy_listeners_, *game_channel_,   *sessions_,        *join_,
                              *addresses_,       *hosts_,           *game_server_binary_, *match_targets_, fs,
                              *platform.system,  random,            redactor,         *requests_,        *ops_,
                              *events_,          workers,           strand},
        std::move(play_options));
    backend_->set_login_observer([this](const backend::LoginObservedEvent& event) { play_->on_login_observed(event); });

    // Guidance.
    guidance_store_ = std::make_unique<StateGuidanceStore>(stores_->state);
    notices_ = std::make_unique<ux::NoticeService>(*guidance_store_, *events_, clock);
    onboarding_ = std::make_unique<ux::Onboarding>(*guidance_store_, *events_);
    std::vector<ux::SearchableSetting> searchable;
    for (const storage::AnyKey* key : settings_registry_->all()) {
        const std::string_view id = key->spec().id;
        searchable.push_back(ux::SearchableSetting{std::string(id), std::string(id.substr(0, id.find('.'))), key->spec().label,
                                                   std::nullopt});
    }
    settings_search_ = std::make_unique<ux::SettingsSearch>(std::move(searchable));
    app_links_ = std::make_unique<ux::AppLinks>();

    // OS integration.
    integration::IntegrationTargets targets;
    switch (kHostOs) {
        case components::ManifestOs::Windows: targets.flavor = integration::EntryFlavor::Windows; break;
        case components::ManifestOs::MacOs: targets.flavor = integration::EntryFlavor::Apple; break;
        case components::ManifestOs::Linux: targets.flavor = integration::EntryFlavor::FreeDesktop; break;
    }
    targets.engine_exe = info.self.image_path;
    integration_ = std::make_unique<integration::IntegrationService>(*platform.integration, stores_->state, workers, strand,
                                                                     *ops_, *events_, std::move(targets));
    prerequisites_ = std::make_unique<integration::PrerequisiteService>(*platform.prerequisites, workers, strand, *ops_, *events_);
    integration::PurgeHooks purge_hooks;
    purge_hooks.find_blockers = [this](integration::PurgeScope scope) {
        integration::PurgeBlockers blockers;
        for (const sessions::SessionInfo& session : sessions_->list())
            if (sessions::is_live(session.phase)) blockers.sessions.push_back(session.id);
        for (const LiveOp& op : ops_->live())
            if (op.kind == OpKind::ComponentEnsure || op.kind == OpKind::RuntimeSetup || op.kind == OpKind::UpdateApply ||
                op.kind == OpKind::Play || op.kind == OpKind::Host)
                blockers.ops.push_back(op.op);
        blockers.backend_running = (scope == integration::PurgeScope::BackendData || scope == integration::PurgeScope::All) &&
                                   backend_->state().phase != backend::BackendPhase::Stopped &&
                                   backend_->state().phase != backend::BackendPhase::Failed;
        return blockers;
    };
    purge_hooks.prepare = [](integration::PurgeScope, UniqueFunction<void()> ready) { ready(); };
    purge_hooks.on_purged = [this](integration::PurgeScope scope) {
        REBOOT_LOG_INFO(Engine, "purged scope {}", static_cast<int>(scope));
        if (scope == integration::PurgeScope::Components || scope == integration::PurgeScope::All)
            components_->load([](Result<void> loaded) { log_failure("reloading the component store", loaded); });
    };
    purge_ = std::make_unique<integration::PurgeService>(fs, layout, install, integration::purge_targets(layout),
                                                         std::move(purge_hooks), workers, strand, *ops_);
    shell_ = std::make_unique<integration::ShellService>(
        *platform.shell, *platform.system,
        kLinuxHost ? integration::DesktopCheck::Display : integration::DesktopCheck::OsSession,
        workers, strand);

    // Resets.
    storage::ResetHooks reset_hooks;
    reset_hooks.find_blockers = [this](storage::ResetGroup group) {
        storage::ResetBlockers blockers;
        for (const sessions::SessionInfo& session : sessions_->list()) {
            if (!sessions::is_live(session.phase)) continue;
            const bool affected = group == storage::ResetGroup::Backend ||
                                  (group == storage::ResetGroup::Play && session.kind == sessions::SessionKind::Play) ||
                                  (group == storage::ResetGroup::Host && session.kind == sessions::SessionKind::Host);
            if (affected) blockers.sessions.push_back(session.id);
        }
        blockers.backend_running = group == storage::ResetGroup::Backend &&
                                   backend_->state().phase != backend::BackendPhase::Stopped &&
                                   backend_->state().phase != backend::BackendPhase::Failed;
        return blockers;
    };
    reset_hooks.stop_blockers = [this](storage::ResetGroup, storage::ResetBlockers blockers, CancelToken,
                                       UniqueFunction<void(Result<void>)> done) {
        const bool backend_running = blockers.backend_running;
        backend_sessions_->stop_sessions(std::move(blockers.sessions), [this, backend_running, done = std::move(done)](
                                                                          Result<void> stopped) mutable {
            if (!stopped || !backend_running) return done(std::move(stopped));
            backend_->stop_for(backend::BackendStopCause::Reset, std::move(done));
        });
    };
    reset_hooks.reset_records = [this](storage::ResetGroup group) -> Result<void> {
        if (group == storage::ResetGroup::Host) return hosts_->reset_profiles();
        return {};
    };
    reset_ = std::make_unique<storage::ResetService>(*settings_, *settings_registry_, *ops_, std::move(reset_hooks));

    // Lifecycle and updates.
    activity_ = std::make_unique<EngineActivityProbe>(*sessions_, *backend_, *publisher_, *ops_, *events_);
    lifecycle_ = std::make_unique<EngineLifecycle>(EngineLifecycleDeps{
        .origin = info.origin,
        .activity = *activity_,
        .sessions = *sessions_,
        .ops = *ops_,
        .shutdown = *shutdown_,
        .drain_hosts = [this](sessions::ShutdownCause cause, UniqueFunction<void()> done) { hosts_->drain(cause, std::move(done)); },
        .resume = stores_->resume,
        .events = *events_,
        .timers = timers,
        .drain_consented = [this]() -> Result<void> { return updates_->drain_consented(); },
        .on_exit = [this](EngineExit exit) {
            if (runtime_.on_exit) runtime_.on_exit(exit);
        }});
    updates::UpdateOptions update_options;
    update_options.installed = info.app_version;
    update_options.origin = info.origin;
    update_options.channel = settings_now.updates.channel;
    update_options.auto_check = settings_now.updates.auto_check;
    const ports::InstallKind install_kind = platform.paths->install_kind();
    update_options.shim_counts_attempts =
        kLinuxHost && (install_kind == ports::InstallKind::Tarball || install_kind == ports::InstallKind::AppImage);
    updates_ = std::make_unique<updates::UpdateService>(
        updates::UpdateServiceDeps{
            .applier = *platform.updater,
            .fs = fs,
            .manifest = *manifest_,
            .downloader = *downloader_,
            .resume = stores_->resume,
            .state = stores_->state,
            .activity = *lifecycle_,
            .workers = workers,
            .strand = strand,
            .clock = clock,
            .timers = timers,
            .ops = *ops_,
            .events = *events_,
            .requests = *requests_,
            .drain_for_update = [this] { lifecycle_->drain_for_update(); },
            .quiesce = [this](UniqueFunction<Result<void>()> apply) { lifecycle_->quiesce_for_update(std::move(apply)); }},
        update_options, layout);

    router_ = std::make_unique<ApiRouter>(ApiRouterDeps{
        .info = info,
        .lifecycle = *lifecycle_,
        .activity = *activity_,
        .events = *events_,
        .ops = *ops_,
        .requests = *requests_,
        .strand = strand,
        .workers = workers,
        .clock = clock,
        .layout = layout,
        .log_ring = runtime_.log_ring,
        .log_exporter = *log_exporter_,
        .errors = *error_router_,
        .settings = *settings_,
        .settings_registry = *settings_registry_,
        .frontend_state = *frontend_state_,
        .reset = *reset_,
        .notices = *notices_,
        .onboarding = *onboarding_,
        .settings_search = *settings_search_,
        .app_links = *app_links_,
        .components = *components_,
        .catalog = *catalog_,
        .support = *support_,
        .library = *library_,
        .installer = *installer_,
        .build_import = *build_import_,
        .runtimes = *runtimes_,
        .secrets = *secrets_,
        .identity = *identity_,
        .backend = *backend_,
        .backend_accounts = *backend_accounts_,
        .sessions = *sessions_,
        .play = *play_,
        .hosts = *hosts_,
        .host_identities = *host_identities_,
        .game_server = *game_server_binary_,
        .browser = *browser_,
        .views = *server_list_,
        .join = *join_,
        .deep_links = *deep_links_,
        .addresses = *addresses_,
        .own_servers = *own_servers_,
        .updates = *updates_,
        .integration = *integration_,
        .prerequisites = *prerequisites_,
        .purge = *purge_,
        .shell = *shell_,
        .security = *platform.security});
}

void EngineServices::register_shutdown_steps() {
    using sessions::ShutdownStep;
    using Done = UniqueFunction<void(Result<void>)>;
    shutdown_->set_action(ShutdownStep::RefuseNew, [this](const sessions::ShutdownStepContext&, Done done) {
        sessions_->refuse_new();
        done(Result<void>{});
    });
    shutdown_->set_action(ShutdownStep::Unpublish, [this](const sessions::ShutdownStepContext&, Done done) {
        publisher_->withdraw_all([done = std::move(done)]() mutable { done(Result<void>{}); });
    });
    shutdown_->set_action(ShutdownStep::UnmapPorts, [this](const sessions::ShutdownStepContext&, Done done) {
        port_mapper_->unmap_all([done = std::move(done)]() mutable { done(Result<void>{}); });
    });
    shutdown_->set_action(ShutdownStep::DrainHosts, [this](const sessions::ShutdownStepContext& context, Done done) {
        hosts_->drain(context.cause, [done = std::move(done)]() mutable { done(Result<void>{}); });
    });
    shutdown_->set_action(ShutdownStep::StopPlay, [this](const sessions::ShutdownStepContext& context, Done done) {
        sessions::StopRequest stop;
        stop.reason = sessions::stop_reason_for(context.cause);
        stop.grace = std::max(context.budget - sessions::kStopKillMargin, std::chrono::milliseconds{0});
        sessions_->stop_all(sessions::SessionKind::Play, std::move(stop),
                            [done = std::move(done)]() mutable { done(Result<void>{}); });
    });
    shutdown_->set_action(ShutdownStep::StopBackend, [this](const sessions::ShutdownStepContext&, Done done) {
        backend_->stop_for(backend::BackendStopCause::Shutdown, std::move(done));
    });
    shutdown_->set_action(ShutdownStep::CloseListeners, [this](const sessions::ShutdownStepContext& context, Done done) {
        if (ipc_)
            ipc_->stop(lifecycle_->restarting() ? contracts::ipc::GoodbyeReason::Restarting
                                                : contracts::ipc::GoodbyeReason::Shutdown);
        game_channel_->close();
        front_->stop(context.budget, [done = std::move(done)]() mutable { done(Result<void>{}); });
    });
    shutdown_->set_action(ShutdownStep::FlushStores, [this](const sessions::ShutdownStepContext&, Done done) {
        struct Join {
            std::size_t left = 3;
            std::optional<Diagnostic> error;
            Done done;
        };
        auto join = std::make_shared<Join>();
        join->done = std::move(done);
        auto one = [join](Result<void> flushed) {
            if (!flushed && !join->error) join->error = std::move(flushed.error());
            if (--join->left > 0) return;
            if (join->error) join->done(std::unexpected(std::move(*join->error)));
            else join->done(Result<void>{});
        };
        stores_->flush_all(one);
        frontend_state_->flush(CancelToken{}, one);
        host_identities_->flush(one);
    });
    shutdown_->set_action(ShutdownStep::FlushLogs, [this](const sessions::ShutdownStepContext&, Done done) {
        runtime_.workers.submit<std::monostate>(
            [](CancelToken) -> Result<std::monostate> {
                Logger::flush();
                return std::monostate{};
            },
            CancelToken{}, runtime_.strand,
            [done = std::move(done)](Result<std::monostate> flushed) mutable {
                if (!flushed) return done(std::unexpected(std::move(flushed.error())));
                done(Result<void>{});
            });
    });
}

void EngineServices::start() {
    register_shutdown_steps();
    stores_->publish_mode_changes(*events_);
    error_router_->set_on_change([this](const logging::BackgroundFailureChange& change) {
        if (change.failure)
            events_->publish(EventKind::NoticeAdded, convert::background_notice(*change.failure),
                             EventScope{change.failure->session, std::nullopt, {}});
        else
            events_->publish(EventKind::NoticeRemoved, convert::background_notice_key(change.id, std::nullopt),
                             EventScope{});
    });

    secrets_->start([this](secrets::SecretsAvailability availability) {
        runtime_.info.secrets_available = availability.available();
        if (ipc_) ipc_->set_secrets_available(availability.available());
    });
    identity_->ensure_records();
    backend_accounts_->register_identity(identity_->get());

    std::vector<storage::EnginePort> ports;
    if (Result<Port> channel = game_channel_->start(); channel)
        ports.push_back(storage::EnginePort{*channel, storage::EnginePortRole::GameChannel});
    else
        error_router_->report(std::move(channel.error()), LogCategory::Play, std::nullopt);
    if (Result<Port> front = front_->start(); front)
        ports.push_back(storage::EnginePort{*front, storage::EnginePortRole::Front});
    else
        error_router_->report(std::move(front.error()), LogCategory::Net, std::nullopt);

    // Whatever a manifest changes reaches the services that keep a copy of it.
    manifest_->add_listener([this](const components::ReleaseManifest&) {
        const browser::RbsbEndpoint edge = browser::select_rbsb_endpoint(manifest_->endpoint_override(), rbsb_expert_override());
        if (!(browser_->endpoint() == edge)) {
            browser_->set_endpoint(edge);
            publisher_->set_edge(edge);
        }
        support::SupportInputs inputs = support_->inputs();
        if (Result<components::PayloadEntry> payload = manifest_->payload())
            if (const components::PayloadFile* dll = payload->find(components::PayloadRole::ClientDll))
                inputs.client_dll_sha256 = dll->file.sha256;
        inputs.runners.clear();
        if (runtime_.platform.session_host) inputs.runners.push_back(support::RunnerPin{ports::RunnerKind::Native, {}});
        for (const ports::RunnerKind kind : runtimes_->supported())
            if (Result<compat::RunnerProfile> profile = runtimes_->profile(kind))
                inputs.runners.push_back(support::RunnerPin{kind, profile->runtime.value});
        support_->set_inputs(std::move(inputs));
    });
    {
        support::SupportInputs inputs = support_->inputs();
        if (runtime_.platform.session_host) inputs.runners.push_back(support::RunnerPin{ports::RunnerKind::Native, {}});
        support_->set_inputs(std::move(inputs));
    }

    reactions_ = events_->subscribe(EventFilter{{EventKind::SettingsChanged, EventKind::IdentityChanged}, std::nullopt, std::nullopt},
                                    kReactionBudget);
    reactions_->set_notify([this] {
        std::vector<EventEnvelope> changes;
        reactions_->drain(changes, std::numeric_limits<std::size_t>::max());
        bool settings_changed = reactions_->take_resync();
        for (const EventEnvelope& change : changes) {
            if (const auto* renamed = std::any_cast<identity::IdentityChangedEvent>(&change.payload))
                backend_accounts_->on_identity_changed(*renamed);
            else if (change.kind == EventKind::SettingsChanged)
                settings_changed = true;
        }
        if (!settings_changed) return;
        const storage::SettingsValues values = settings_->snapshot().values;
        updates_->set_channel(values.updates.channel);
        updates_->set_auto_check(values.updates.auto_check);
        Result<backend::BackendConfig> config = backend::BackendConfig::from_settings(values.backend);
        const backend::BackendState& state = backend_->state();
        if (config && !(*config == state.config) && !(state.pending_config && *state.pending_config == *config))
            if (Result<backend::ReconfigureTiming> applied = backend_->reconfigure(*config, backend::RunningPolicy::Refuse); !applied)
                REBOOT_LOG_INFO(Backend, "the new backend settings wait for the sessions: {}", applied.error().id);
    });

    port_mapper_->sweep_stale({}, [] {});
    lifecycle_->start();
    updates_->start();

    log_failure("recording the engine's ports", runtime_recorder_->set_ports(std::move(ports)));
}

void EngineServices::apply_resume(const updates::StartupResume& resume, Result<void> self_test) {
    if (resume.verdict == updates::MarkerVerdict::SelfTest) {
        if (Result<void> confirmed = updates_->confirm_startup(std::move(self_test)); !confirmed) {
            REBOOT_LOG_ERROR(Update, "the updated engine failed its self-test: {}", confirmed.error().id);
            if (runtime_.on_exit) runtime_.on_exit(EngineExit::Restart);
            return;
        }
    }
    if (!resume.resume) return;
    for (const HostProfileId& profile : resume.resume->relaunch_hosts) {
        host::HostStartRequest request;
        request.profile = profile;
        // It hosted before the restart, so its tier was already accepted.
        request.untested_confirmed = true;
        if (Result<OpHandle> started = hosts_->start(std::move(request)); !started)
            error_router_->report(std::move(started.error()), LogCategory::Host, std::nullopt);
    }
}

Result<void> EngineServices::write_runtime() { return runtime_recorder_->write_started(runtime_.info); }

void EngineServices::on_os_signal() { lifecycle_->on_os_signal(); }

}  // namespace rb::engine
