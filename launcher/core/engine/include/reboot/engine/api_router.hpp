#pragma once

#include <memory>
#include <optional>
#include <span>

#include "reboot/api/call_context.hpp"
#include "reboot/api/v1/backend.hpp"
#include "reboot/api/v1/browser.hpp"
#include "reboot/api/v1/catalog.hpp"
#include "reboot/api/v1/components.hpp"
#include "reboot/api/v1/dispatch.hpp"
#include "reboot/api/v1/engine.hpp"
#include "reboot/api/v1/guidance.hpp"
#include "reboot/api/v1/host.hpp"
#include "reboot/api/v1/identity.hpp"
#include "reboot/api/v1/install.hpp"
#include "reboot/api/v1/integration.hpp"
#include "reboot/api/v1/join.hpp"
#include "reboot/api/v1/library.hpp"
#include "reboot/api/v1/logs.hpp"
#include "reboot/api/v1/play.hpp"
#include "reboot/api/v1/requests.hpp"
#include "reboot/api/v1/secrets.hpp"
#include "reboot/api/v1/sessions.hpp"
#include "reboot/api/v1/settings.hpp"
#include "reboot/api/v1/support.hpp"
#include "reboot/api/v1/updates.hpp"
#include "reboot/contracts/ipc.hpp"
#include "reboot/engine/started_op.hpp"
#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/events.hpp"
#include "reboot/foundation/flat_map.hpp"
#include "reboot/foundation/operation.hpp"
#include "reboot/foundation/secret.hpp"
#include "reboot/foundation/types.hpp"
#include "reboot/ipc/api_dispatcher.hpp"
#include "reboot/ipc/connection_info.hpp"

namespace rb {
class EventBus;
class Executor;
class IClock;
class OpRegistry;
class UserRequestRegistry;
class WorkerPool;
struct InstallLayout;
class AppLayout;
}  // namespace rb

namespace rb::ports {
class ISecurityProductProbe;
}

namespace rb::logging {
class ErrorRouter;
class LogExporter;
class LogRing;
}  // namespace rb::logging

namespace rb::storage {
class FrontendStateStore;
class ResetService;
class Settings;
class SettingsRegistry;
}  // namespace rb::storage

namespace rb::ux {
class AppLinks;
class NoticeService;
class Onboarding;
class SettingsSearch;
}  // namespace rb::ux

namespace rb::components {
class ComponentStore;
}

namespace rb::catalog {
class CatalogService;
}

namespace rb::support {
class SupportPolicy;
}

namespace rb::builds {
class BuildInstaller;
class ImportService;
class Library;
}  // namespace rb::builds

namespace rb::compat {
class RuntimeService;
}

namespace rb::secrets {
class SecretService;
}

namespace rb::identity {
class IdentityService;
}

namespace rb::backend {
class BackendAccounts;
class BackendService;
}  // namespace rb::backend

namespace rb::sessions {
class SessionRegistry;
}

namespace rb::gameserver {
class GameServerBinary;
}

namespace rb::publish {
class HostIdentityStore;
}

namespace rb::play {
class PlayService;
}

namespace rb::host {
class HostService;
}

namespace rb::browser {
class BrowserSession;
class DeepLinkService;
class GameServerTarget;
class IOwnServers;
class JoinService;
class ServerList;
}  // namespace rb::browser

namespace rb::updates {
class UpdateService;
}

namespace rb::integration {
class IntegrationService;
class PrerequisiteService;
class PurgeService;
class ShellService;
}  // namespace rb::integration

namespace rb::engine {

class EngineActivityProbe;
class EngineLifecycle;
struct EngineInfo;

// The services the API reaches, and nothing else of the composition root.
struct ApiRouterDeps {
    const EngineInfo& info;
    EngineLifecycle& lifecycle;
    EngineActivityProbe& activity;
    EventBus& events;
    OpRegistry& ops;
    UserRequestRegistry& requests;
    Executor& strand;
    WorkerPool& workers;
    const IClock& clock;
    const AppLayout& layout;
    logging::LogRing& log_ring;
    logging::LogExporter& log_exporter;
    // Background failures are listed as notices and acknowledged by dismissing them.
    logging::ErrorRouter& errors;
    storage::Settings& settings;
    const storage::SettingsRegistry& settings_registry;
    storage::FrontendStateStore& frontend_state;
    storage::ResetService& reset;
    ux::NoticeService& notices;
    ux::Onboarding& onboarding;
    ux::SettingsSearch& settings_search;
    ux::AppLinks& app_links;
    components::ComponentStore& components;
    catalog::CatalogService& catalog;
    const support::SupportPolicy& support;
    builds::Library& library;
    builds::BuildInstaller& installer;
    builds::ImportService& build_import;
    compat::RuntimeService& runtimes;
    secrets::SecretService& secrets;
    identity::IdentityService& identity;
    backend::BackendService& backend;
    backend::BackendAccounts& backend_accounts;
    sessions::SessionRegistry& sessions;
    play::PlayService& play;
    host::HostService& hosts;
    publish::HostIdentityStore& host_identities;
    const gameserver::GameServerBinary& game_server;
    browser::BrowserSession& browser;
    browser::ServerList& views;
    browser::JoinService& join;
    browser::DeepLinkService& deep_links;
    browser::GameServerTarget& addresses;
    // ServerEntry.own.
    const browser::IOwnServers& own_servers;
    updates::UpdateService& updates;
    integration::IntegrationService& integration;
    integration::PrerequisiteService& prerequisites;
    integration::PurgeService& purge;
    integration::ShellService& shell;
    ports::ISecurityProductProbe& security;
};

// Capabilities: none. Strand-only; the only place domain types, events and UserRequest payloads
// become reboot.api.v1 messages.
// - Play's display comes from the connection's Hello, unless the request carries the caller's
//   environment for this launch.
// - Calls answer at once: what a service reads off the strand (integration status, prerequisites,
//   a shell's frontend state) is kept up to date here, and is engine.not_ready until first read.
// - Each started op's method id is kept, so its Outcome carries that method's response; its
//   request is kept too, for Engine.operations and the OpStarted event.
// - EventKind::ForegroundHint is no API event, so encode_event skips it.
class ApiRouter final : public ipc::IApiDispatcher,
                        public api::IBackendHandler,
                        public api::IBrowserHandler,
                        public api::ICatalogHandler,
                        public api::IComponentsHandler,
                        public api::IEngineHandler,
                        public api::IGuidanceHandler,
                        public api::IHostHandler,
                        public api::IIdentityHandler,
                        public api::IInstallHandler,
                        public api::IIntegrationHandler,
                        public api::IJoinHandler,
                        public api::ILibraryHandler,
                        public api::ILogsHandler,
                        public api::IPlayHandler,
                        public api::IRequestsHandler,
                        public api::ISecretsHandler,
                        public api::ISessionsHandler,
                        public api::ISettingsHandler,
                        public api::ISupportHandler,
                        public api::IUpdatesHandler {
public:
    explicit ApiRouter(ApiRouterDeps deps);
    ~ApiRouter() override;
    ApiRouter(const ApiRouter&) = delete;
    ApiRouter& operator=(const ApiRouter&) = delete;

    // ipc::IApiDispatcher
    void on_connected(const ipc::ConnectionInfo& connection) override;
    void on_disconnected(ConnectionId connection) override;
    Result<contracts::ipc::Bytes> call(const ipc::ConnectionInfo& from, u32 method_id, std::span<const u8> request) override;
    Result<OpHandle> start(const ipc::ConnectionInfo& from, u32 method_id, std::span<const u8> request,
                           std::optional<DisconnectPolicy> disconnect) override;
    [[nodiscard]] contracts::ipc::Bytes encode_outcome(OpId op, const ErasedOutcome& outcome) override;
    Result<EventFilter> decode_filter(std::span<const u8> filter) override;
    [[nodiscard]] std::optional<contracts::ipc::WireEvent> encode_event(const EventEnvelope& event) override;
    Result<void> put_secret(const ipc::ConnectionInfo& from, std::span<const u8> target, SecretBytes secret) override;
    Result<SecretBytes> reveal_secret(const ipc::ConnectionInfo& from, std::span<const u8> target) override;

    // Backend
    Result<api::BackendStatusResponse> status(const api::CallContext& context, const api::BackendStatusRequest& request) override;
    Result<api::BackendDataDirResponse> data_dir(const api::CallContext& context, const api::BackendDataDirRequest& request) override;
    Result<api::BackendSetTargetResponse> set_target(const api::CallContext& context, const api::BackendSetTargetRequest& request) override;
    Result<api::BackendAccountsListResponse> accounts_list(const api::CallContext& context, const api::BackendAccountsListRequest& request) override;
    Result<OpHandle> start(const api::CallContext& context, const api::BackendStartRequest& request, DisconnectPolicy disconnect) override;
    Result<OpHandle> start_stop(const api::CallContext& context, const api::BackendStopRequest& request, DisconnectPolicy disconnect) override;
    Result<OpHandle> start_accounts_reset(const api::CallContext& context, const api::BackendAccountsResetRequest& request, DisconnectPolicy disconnect) override;
    Result<OpHandle> start_accounts_delete(const api::CallContext& context, const api::BackendAccountsDeleteRequest& request, DisconnectPolicy disconnect) override;
    Result<OpHandle> start_accounts_prune(const api::CallContext& context, const api::BackendAccountsPruneRequest& request, DisconnectPolicy disconnect) override;
    Result<OpHandle> start_accounts_rename(const api::CallContext& context, const api::BackendAccountsRenameRequest& request, DisconnectPolicy disconnect) override;
    // Browser
    Result<api::BrowserOpenViewResponse> open_view(const api::CallContext& context, const api::BrowserOpenViewRequest& request) override;
    Result<api::BrowserUpdateViewResponse> update_view(const api::CallContext& context, const api::BrowserUpdateViewRequest& request) override;
    Result<api::BrowserCloseViewResponse> close_view(const api::CallContext& context, const api::BrowserCloseViewRequest& request) override;
    Result<api::BrowserStateResponse> state(const api::CallContext& context, const api::BrowserStateRequest& request) override;
    Result<OpHandle> start_resolve(const api::CallContext& context, const api::BrowserResolveRequest& request, DisconnectPolicy disconnect) override;
    // Catalog
    Result<api::CatalogListResponse> list(const api::CallContext& context, const api::CatalogListRequest& request) override;
    Result<OpHandle> start_refresh(const api::CallContext& context, const api::CatalogRefreshRequest& request, DisconnectPolicy disconnect) override;
    // Components
    Result<api::ComponentsListResponse> list(const api::CallContext& context, const api::ComponentsListRequest& request) override;
    Result<OpHandle> start_ensure(const api::CallContext& context, const api::ComponentsEnsureRequest& request, DisconnectPolicy disconnect) override;
    Result<OpHandle> start_remove(const api::CallContext& context, const api::ComponentsRemoveRequest& request, DisconnectPolicy disconnect) override;
    Result<OpHandle> start_runtime_setup(const api::CallContext& context, const api::ComponentsRuntimeSetupRequest& request, DisconnectPolicy disconnect) override;
    // Engine
    Result<api::EngineStatusResponse> status(const api::CallContext& context, const api::EngineStatusRequest& request) override;
    Result<api::EngineInfoResponse> info(const api::CallContext& context, const api::EngineInfoRequest& request) override;
    Result<api::EngineDrainResponse> drain(const api::CallContext& context, const api::EngineDrainRequest& request) override;
    Result<api::EngineShutdownResponse> shutdown(const api::CallContext& context, const api::EngineShutdownRequest& request) override;
    Result<api::EngineRestartWhenIdleResponse> restart_when_idle(const api::CallContext& context, const api::EngineRestartWhenIdleRequest& request) override;
    Result<api::EngineOperationsResponse> operations(const api::CallContext& context, const api::EngineOperationsRequest& request) override;
    // Guidance
    Result<api::GuidanceNoticesListResponse> notices_list(const api::CallContext& context, const api::GuidanceNoticesListRequest& request) override;
    Result<api::GuidanceNoticesDismissResponse> notices_dismiss(const api::CallContext& context, const api::GuidanceNoticesDismissRequest& request) override;
    Result<api::GuidanceOnboardingStateResponse> onboarding_state(const api::CallContext& context, const api::GuidanceOnboardingStateRequest& request) override;
    Result<api::GuidanceOnboardingAdvanceResponse> onboarding_advance(const api::CallContext& context, const api::GuidanceOnboardingAdvanceRequest& request) override;
    Result<api::GuidanceOnboardingSkipResponse> onboarding_skip(const api::CallContext& context, const api::GuidanceOnboardingSkipRequest& request) override;
    Result<api::GuidanceLinksResponse> links(const api::CallContext& context, const api::GuidanceLinksRequest& request) override;
    // Host
    Result<api::HostProfilesListResponse> profiles_list(const api::CallContext& context, const api::HostProfilesListRequest& request) override;
    Result<api::HostProfilesCreateResponse> profiles_create(const api::CallContext& context, const api::HostProfilesCreateRequest& request) override;
    Result<api::HostProfilesUpdateResponse> profiles_update(const api::CallContext& context, const api::HostProfilesUpdateRequest& request) override;
    Result<api::HostProfilesDeleteResponse> profiles_delete(const api::CallContext& context, const api::HostProfilesDeleteRequest& request) override;
    Result<api::HostShareLinkResponse> share_link(const api::CallContext& context, const api::HostShareLinkRequest& request) override;
    Result<api::HostCommandResponse> command(const api::CallContext& context, const api::HostCommandRequest& request) override;
    Result<OpHandle> start(const api::CallContext& context, const api::HostStartRequest& request, DisconnectPolicy disconnect) override;
    Result<api::HostStatusResponse> status(const api::CallContext& context, const api::HostStatusRequest& request) override;
    Result<api::HostCancelMatchEndResponse> cancel_match_end(const api::CallContext& context, const api::HostCancelMatchEndRequest& request) override;
    Result<OpHandle> start_identity_export(const api::CallContext& context, const api::HostIdentityExportRequest& request, DisconnectPolicy disconnect) override;
    Result<OpHandle> start_identity_import(const api::CallContext& context, const api::HostIdentityImportRequest& request, DisconnectPolicy disconnect) override;
    // Identity
    Result<api::IdentityGetResponse> get(const api::CallContext& context, const api::IdentityGetRequest& request) override;
    Result<api::IdentitySetDisplayNameResponse> set_display_name(const api::CallContext& context, const api::IdentitySetDisplayNameRequest& request) override;
    Result<api::IdentityResetResponse> reset(const api::CallContext& context, const api::IdentityResetRequest& request) override;
    // Install
    Result<OpHandle> start_suggest_destination(const api::CallContext& context, const api::InstallSuggestDestinationRequest& request, DisconnectPolicy disconnect) override;
    Result<OpHandle> start_install(const api::CallContext& context, const api::InstallInstallRequest& request, DisconnectPolicy disconnect) override;
    Result<OpHandle> start_discard_staging(const api::CallContext& context, const api::InstallDiscardStagingRequest& request, DisconnectPolicy disconnect) override;
    Result<OpHandle> start_delete_unregistered(const api::CallContext& context, const api::InstallDeleteUnregisteredRequest& request, DisconnectPolicy disconnect) override;
    // Integration
    Result<api::IntegrationStatusResponse> status(const api::CallContext& context, const api::IntegrationStatusRequest& request) override;
    Result<api::IntegrationPrerequisitesResponse> prerequisites(const api::CallContext& context, const api::IntegrationPrerequisitesRequest& request) override;
    Result<api::IntegrationShellOpenUrlResponse> shell_open_url(const api::CallContext& context, const api::IntegrationShellOpenUrlRequest& request) override;
    Result<api::IntegrationShellOpenPathResponse> shell_open_path(const api::CallContext& context, const api::IntegrationShellOpenPathRequest& request) override;
    Result<api::IntegrationShellRevealResponse> shell_reveal(const api::CallContext& context, const api::IntegrationShellRevealRequest& request) override;
    Result<OpHandle> start_apply(const api::CallContext& context, const api::IntegrationApplyRequest& request, DisconnectPolicy disconnect) override;
    Result<OpHandle> start_remove(const api::CallContext& context, const api::IntegrationRemoveRequest& request, DisconnectPolicy disconnect) override;
    Result<OpHandle> start_remediate(const api::CallContext& context, const api::IntegrationRemediateRequest& request, DisconnectPolicy disconnect) override;
    Result<OpHandle> start_purge(const api::CallContext& context, const api::IntegrationPurgeRequest& request, DisconnectPolicy disconnect) override;
    // Join
    Result<api::JoinParseAddressResponse> parse_address(const api::CallContext& context, const api::JoinParseAddressRequest& request) override;
    Result<api::JoinTargetResponse> target(const api::CallContext& context, const api::JoinTargetRequest& request) override;
    Result<api::JoinSetCustomTargetResponse> set_custom_target(const api::CallContext& context, const api::JoinSetCustomTargetRequest& request) override;
    Result<api::JoinClearTargetResponse> clear_target(const api::CallContext& context, const api::JoinClearTargetRequest& request) override;
    Result<OpHandle> start_resolve_link(const api::CallContext& context, const api::JoinResolveLinkRequest& request, DisconnectPolicy disconnect) override;
    Result<OpHandle> start_grant(const api::CallContext& context, const api::JoinGrantRequest& request, DisconnectPolicy disconnect) override;
    // Library
    Result<api::LibraryListResponse> list(const api::CallContext& context, const api::LibraryListRequest& request) override;
    Result<api::LibraryGetResponse> get(const api::CallContext& context, const api::LibraryGetRequest& request) override;
    Result<api::LibrarySelectResponse> select(const api::CallContext& context, const api::LibrarySelectRequest& request) override;
    Result<api::LibraryUpdateResponse> update(const api::CallContext& context, const api::LibraryUpdateRequest& request) override;
    Result<OpHandle> start_relocate(const api::CallContext& context, const api::LibraryRelocateRequest& request, DisconnectPolicy disconnect) override;
    Result<OpHandle> start_import(const api::CallContext& context, const api::LibraryImportRequest& request, DisconnectPolicy disconnect) override;
    Result<OpHandle> start_remove(const api::CallContext& context, const api::LibraryRemoveRequest& request, DisconnectPolicy disconnect) override;
    // Logs
    Result<api::LogsReadResponse> read(const api::CallContext& context, const api::LogsReadRequest& request) override;
    Result<OpHandle> start_export(const api::CallContext& context, const api::LogsExportRequest& request, DisconnectPolicy disconnect) override;
    // Play
    Result<api::PlayPlanResponse> plan(const api::CallContext& context, const api::PlayPlanRequest& request) override;
    Result<OpHandle> start(const api::CallContext& context, const api::PlayStartRequest& request, DisconnectPolicy disconnect) override;
    // Requests
    Result<api::RequestsPendingResponse> pending(const api::CallContext& context, const api::RequestsPendingRequest& request) override;
    Result<api::RequestsRespondResponse> respond(const api::CallContext& context, const api::RequestsRespondRequest& request) override;
    // Secrets
    Result<api::SecretsStateResponse> state(const api::CallContext& context, const api::SecretsStateRequest& request) override;
    Result<api::SecretsClearResponse> clear(const api::CallContext& context, const api::SecretsClearRequest& request) override;
    // Sessions
    Result<api::SessionsListResponse> list(const api::CallContext& context, const api::SessionsListRequest& request) override;
    Result<api::SessionsGetResponse> get(const api::CallContext& context, const api::SessionsGetRequest& request) override;
    Result<api::SessionsSetLeaseResponse> set_lease(const api::CallContext& context, const api::SessionsSetLeaseRequest& request) override;
    Result<OpHandle> start_stop(const api::CallContext& context, const api::SessionsStopRequest& request, DisconnectPolicy disconnect) override;
    // Settings
    Result<api::SettingsSnapshotResponse> snapshot(const api::CallContext& context, const api::SettingsSnapshotRequest& request) override;
    Result<api::SettingsPatchResponse> patch(const api::CallContext& context, const api::SettingsPatchRequest& request) override;
    Result<api::SettingsResetResponse> reset(const api::CallContext& context, const api::SettingsResetRequest& request) override;
    Result<api::SettingsSearchResponse> search(const api::CallContext& context, const api::SettingsSearchRequest& request) override;
    Result<api::SettingsFrontendStateGetResponse> frontend_state_get(const api::CallContext& context, const api::SettingsFrontendStateGetRequest& request) override;
    Result<api::SettingsFrontendStatePutResponse> frontend_state_put(const api::CallContext& context, const api::SettingsFrontendStatePutRequest& request) override;
    Result<api::SettingsLanguagesResponse> languages(const api::CallContext& context, const api::SettingsLanguagesRequest& request) override;
    Result<api::SettingsDescriptorsResponse> descriptors(const api::CallContext& context, const api::SettingsDescriptorsRequest& request) override;
    Result<api::SettingsConsoleKeysResponse> console_keys(const api::CallContext& context, const api::SettingsConsoleKeysRequest& request) override;
    Result<api::SettingsConsoleKeyFromNativeResponse> console_key_from_native(const api::CallContext& context, const api::SettingsConsoleKeyFromNativeRequest& request) override;
    // Support
    Result<api::SupportQueryResponse> query(const api::CallContext& context, const api::SupportQueryRequest& request) override;
    // Updates
    Result<api::UpdatesStatusResponse> status(const api::CallContext& context, const api::UpdatesStatusRequest& request) override;
    Result<OpHandle> start_check(const api::CallContext& context, const api::UpdatesCheckRequest& request, DisconnectPolicy disconnect) override;
    Result<OpHandle> start_apply(const api::CallContext& context, const api::UpdatesApplyRequest& request, DisconnectPolicy disconnect) override;

private:
    struct State;

    [[nodiscard]] api::Handlers handlers() noexcept;
    // Records the op for encode_outcome and Engine.operations, and publishes OpStarted.
    Result<OpHandle> started(Result<OpHandle> handle, u32 method_id, std::span<const u8> request,
                             DisconnectPolicy disconnect);
    [[nodiscard]] u32 method_of(OpId op) const noexcept;

    ApiRouterDeps deps_;
    // Each live connection's Hello, which outlives the calls made on it.
    FlatMap<ConnectionId, ipc::ConnectionInfo> connections_;
    FlatMap<OpId, StartedOp> started_ops_;
    std::unique_ptr<State> state_;
};

}  // namespace rb::engine
