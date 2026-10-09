#include "reboot/engine/api_router.hpp"

#include <algorithm>
#include <cctype>
#include <limits>
#include <string>
#include <utility>
#include <vector>

#include "api_convert.hpp"
#include "api_requests.hpp"
#include "api_router_state.hpp"
#include "messages.hpp"
#include "reboot/api/v1/events.hpp"
#include "reboot/api/v1/method_table.hpp"
#include "reboot/backend/backend_accounts_changed.hpp"
#include "reboot/backend/backend_upstream.hpp"
#include "reboot/browser/deep_link_service.hpp"
#include "reboot/browser/join_outcome.hpp"
#include "reboot/browser/server_list.hpp"
#include "reboot/builds/import_outcome.hpp"
#include "reboot/builds/install_outcome.hpp"
#include "reboot/builds/library_changed_event.hpp"
#include "reboot/catalog/catalog_updated.hpp"
#include "reboot/components/component_changed_event.hpp"
#include "reboot/components/component_store.hpp"
#include "reboot/engine/engine_activity_probe.hpp"
#include "reboot/foundation/executor.hpp"
#include "reboot/foundation/log.hpp"
#include "reboot/front/xmpp_unavailable.hpp"
#include "reboot/host/host_profiles_changed.hpp"
#include "reboot/host/host_service.hpp"
#include "reboot/identity/identity_changed_event.hpp"
#include "reboot/integration/integration_changed.hpp"
#include "reboot/integration/integration_service.hpp"
#include "reboot/integration/prerequisite_service.hpp"
#include "reboot/integration/prerequisites_changed.hpp"
#include "reboot/integration/purge_report.hpp"
#include "reboot/logging/log_exporter.hpp"
#include "reboot/logging/log_line_forwarder.hpp"
#include "reboot/net/port_mapping.hpp"
#include "reboot/ports/os_services.hpp"
#include "reboot/publish/publish_state_changed.hpp"
#include "reboot/secrets/secret_service.hpp"
#include "reboot/secrets/secret_state_changed_event.hpp"
#include "reboot/sessions/session_registry.hpp"
#include "reboot/storage/frontend_state_store.hpp"
#include "reboot/storage/settings_changed.hpp"
#include "reboot/storage/shell_name.hpp"
#include "reboot/updates/update_event.hpp"
#include "reboot/ux/notice.hpp"
#include "reboot/ux/onboarding.hpp"

namespace rb::engine {

namespace {

constexpr std::size_t kWatchBudget = std::size_t{1} << 20;

[[nodiscard]] std::string lowercase(std::string_view text) {
    std::string out(text);
    for (char& c : out) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return out;
}

[[nodiscard]] bool matches_text(const browser::ServerRow& row, const std::string& lowered) {
    if (lowered.empty()) return true;
    return lowercase(row.name).find(lowered) != std::string::npos ||
           lowercase(row.author).find(lowered) != std::string::npos;
}

[[nodiscard]] api::LogLine log_line(const logging::LogLineEvent& event) {
    api::LogLine out;
    out.entry = convert::log_entry(event.entry);
    return out;
}

}  // namespace

ApiRouter::ApiRouter(ApiRouterDeps deps) : deps_(deps), state_(std::make_unique<State>()) {
    state_->catalog = ux::MessageCatalog::english_from_registry();
    state_->watch = deps_.events.subscribe(EventFilter{{EventKind::IntegrationChanged, EventKind::PrerequisitesChanged,
                                                        EventKind::ComponentChanged},
                                                       std::nullopt,
                                                       std::nullopt},
                                           kWatchBudget);
    state_->watch->set_notify([this] {
        std::vector<EventEnvelope> events;
        state_->watch->drain(events, std::numeric_limits<std::size_t>::max());
        // A lost update is read again; nothing here is replayed from the events.
        if (state_->watch->take_resync()) {
            state_->integration.reset();
            state_->prerequisites.reset();
        }
        for (const EventEnvelope& event : events) {
            if (const auto* changed = std::any_cast<integration::IntegrationChanged>(&event.payload)) {
                state_->integration = changed->entries;
            } else if (const auto* checked = std::any_cast<integration::PrerequisitesChanged>(&event.payload)) {
                state_->prerequisites = checked->prerequisites;
            } else if (const auto* component = std::any_cast<components::ComponentChangedEvent>(&event.payload)) {
                if (component->problem && component->problem->recovery != components::RecoveryState::Recovered)
                    state_->component_problems.insert_or_assign(component->info.ref.id, *component->problem);
                else
                    state_->component_problems.erase(component->info.ref.id);
            }
        }
    });

    const CancelToken alive = state_->alive.token();
    state_->integration_reading = true;
    deps_.integration.status(alive, [this, alive](std::vector<integration::EntryStatus> entries) {
        if (alive.cancelled()) return;
        state_->integration = std::move(entries);
        state_->integration_reading = false;
    });
    state_->prerequisites_reading = true;
    deps_.prerequisites.check(alive, [this, alive](std::vector<integration::Prerequisite> checked) {
        if (alive.cancelled()) return;
        state_->prerequisites = std::move(checked);
        state_->prerequisites_reading = false;
    });
    state_->security_reading = true;
    deps_.workers.submit<std::optional<ports::SecurityProducts>>(
        [&security = deps_.security](CancelToken) { return security.probe(); }, alive, deps_.strand,
        [this, alive](Result<std::optional<ports::SecurityProducts>> probed) {
            if (alive.cancelled()) return;
            state_->security_reading = false;
            if (probed) state_->security = probed->value_or(ports::SecurityProducts{});
            else REBOOT_LOG_INFO(Engine, "the security product probe failed: {}", probed.error().id);
        });
}

ApiRouter::~ApiRouter() {
    state_->alive.cancel(CancelReason::Shutdown);
    state_->watch->set_notify({});
    for (auto& [op, relay] : state_->relays) relay.inner_events->set_notify({});
}

api::Handlers ApiRouter::handlers() noexcept {
    return api::Handlers{*this, *this, *this, *this, *this, *this, *this, *this, *this, *this,
                         *this, *this, *this, *this, *this, *this, *this, *this, *this, *this};
}

u32 ApiRouter::method_of(OpId op) const noexcept {
    const auto it = started_ops_.find(op);
    return it == started_ops_.end() ? 0 : it->second.method_id;
}

void ApiRouter::on_connected(const ipc::ConnectionInfo& connection) {
    connections_.insert_or_assign(connection.id, connection);
    deps_.lifecycle.on_connected(connection.id, connection.client_kind);

    // Its shell's state is read now, so the UI's first frontend_state_get finds it.
    std::string_view shell;
    switch (connection.client_kind) {
        case contracts::ipc::ClientKind::WindowsGui: shell = "windows"; break;
        case contracts::ipc::ClientKind::MacGui: shell = "macos"; break;
        case contracts::ipc::ClientKind::LinuxGui: shell = "linux"; break;
        case contracts::ipc::ClientKind::Cli: shell = "cli"; break;
        case contracts::ipc::ClientKind::Unknown:
        case contracts::ipc::ClientKind::Test: break;
    }
    if (shell.empty() || state_->frontend.contains(std::string(shell))) return;
    Result<storage::ShellName> name = storage::ShellName::parse(shell);
    if (!name) return;
    const CancelToken alive = state_->alive.token();
    deps_.frontend_state.get(*name, alive, [this, alive, key = std::string(shell)](Result<std::vector<u8>> blob) {
        if (alive.cancelled() || !blob || state_->frontend.contains(key)) return;
        state_->frontend.insert_or_assign(key, std::move(*blob));
    });
}

void ApiRouter::on_disconnected(ConnectionId connection) {
    connections_.erase(connection);
    deps_.sessions.on_connection_closed(connection);
    for (auto it = state_->views.begin(); it != state_->views.end();) {
        if (it->second.owner == connection) {
            deps_.views.close(browser::ViewId{it->first});
            it = state_->views.erase(it);
        } else {
            ++it;
        }
    }
    if (state_->views.empty()) state_->browser_lease.release();
    deps_.lifecycle.on_disconnected(connection);
}

Result<contracts::ipc::Bytes> ApiRouter::call(const ipc::ConnectionInfo& from, u32 method_id,
                                              std::span<const u8> request) {
    const api::CallContext context{from.id, from.client_kind, from.caller};
    return api::dispatch_call(handlers(), context, method_id, request);
}

Result<OpHandle> ApiRouter::start(const ipc::ConnectionInfo& from, u32 method_id, std::span<const u8> request,
                                  std::optional<DisconnectPolicy> disconnect) {
    // A stop adds no work, and a drain may be waiting on it.
    const bool stops = method_id == api::kSessionsStop || method_id == api::kBackendStop;
    if (!stops)
        if (Result<void> admitted = deps_.lifecycle.admit_new_work(); !admitted) return std::unexpected(admitted.error());
    const api::MethodSpec* spec = api::MethodTable::find(method_id);
    const DisconnectPolicy policy =
        disconnect.value_or(spec != nullptr ? spec->default_disconnect : DisconnectPolicy::BoundToConnection);
    const api::CallContext context{from.id, from.client_kind, from.caller};
    return started(api::dispatch_start(handlers(), context, method_id, request, policy), method_id, request, policy);
}

Result<OpHandle> ApiRouter::started(Result<OpHandle> handle, u32 method_id, std::span<const u8> request,
                                    DisconnectPolicy disconnect) {
    if (!handle) return handle;
    // Ops whose outcome the registry no longer keeps are forgotten here.
    const std::vector<LiveOp> live = deps_.ops.live();
    for (auto it = started_ops_.begin(); it != started_ops_.end();) {
        const bool is_live = std::ranges::any_of(live, [&](const LiveOp& entry) { return entry.op == it->first; });
        if (!is_live && !deps_.ops.outcome(it->first)) it = started_ops_.erase(it);
        else ++it;
    }
    StartedOp op;
    op.op = handle->id();
    op.method_id = method_id;
    op.request.assign(request.begin(), request.end());
    op.disconnect = disconnect;
    for (const LiveOp& entry : live)
        if (entry.op == op.op) op.session = entry.session;
    op.started_at = deps_.clock.system_now();
    started_ops_.insert_or_assign(op.op, op);
    deps_.events.publish(EventKind::OpStarted, std::move(op), EventScope{std::nullopt, handle->id(), {}});
    deps_.activity.notify_changed();
    return handle;
}

namespace {

// What the op completed with, as its method's response; an outcome that is really a failure is one.
[[nodiscard]] Result<std::any> response_for(const ApiRouterDeps& deps, u32 method_id, const std::any& value) {
    const auto bug = [] { return std::unexpected(internal_bug("engine.response_for")); };
    switch (method_id) {
        case api::kBackendStart: {
            const auto* upstream = std::any_cast<backend::BackendUpstream>(&value);
            if (upstream == nullptr) return bug();
            return std::any(api::BackendStartResponse{upstream->http_port.value_or(0)});
        }
        case api::kBackendAccountsPrune: {
            const auto* removed = std::any_cast<u64>(&value);
            if (removed == nullptr) return bug();
            return std::any(api::BackendAccountsPruneResponse{
                static_cast<u32>(std::min<u64>(*removed, std::numeric_limits<u32>::max()))});
        }
        case api::kBackendAccountsRename: {
            const auto* account = std::any_cast<std::string>(&value);
            if (account == nullptr) return bug();
            return std::any(api::BackendAccountsRenameResponse{*account});
        }
        case api::kCatalogRefresh: {
            const auto* updated = std::any_cast<catalog::CatalogUpdated>(&value);
            if (updated == nullptr) return bug();
            return std::any(api::CatalogRefreshResponse{updated->serial, convert::catalog_source(updated->origin)});
        }
        case api::kComponentsEnsure: {
            const auto* ref = std::any_cast<components::ComponentRef>(&value);
            if (ref == nullptr) return bug();
            api::ComponentsEnsureResponse response;
            for (api::Component& component : convert::components(deps.components.list()))
                if (component.id == ref->id) response.component = std::move(component);
            if (response.component.id.empty()) response.component.id = ref->id;
            return std::any(std::move(response));
        }
        case api::kComponentsRuntimeSetup: {
            const auto* refs = std::any_cast<std::vector<components::ComponentRef>>(&value);
            if (refs == nullptr) return bug();
            api::ComponentsRuntimeSetupResponse response;
            for (api::Component& component : convert::components(deps.components.list()))
                if (std::ranges::any_of(*refs, [&](const components::ComponentRef& ref) { return ref.id == component.id; }))
                    response.components.push_back(std::move(component));
            return std::any(std::move(response));
        }
        case api::kHostStart:
        case api::kPlayStart: {
            const auto* session = std::any_cast<SessionId>(&value);
            if (session == nullptr) return bug();
            if (method_id == api::kHostStart) return std::any(api::HostStartResponse{convert::id(*session)});
            return std::any(api::PlayStartResponse{convert::id(*session)});
        }
        case api::kHostIdentityImport: {
            const auto* server = std::any_cast<ServerId>(&value);
            if (server == nullptr) return bug();
            return std::any(api::HostIdentityImportResponse{convert::id(*server)});
        }
        case api::kInstallSuggestDestination: {
            const auto* suggestion = std::any_cast<builds::DestinationSuggestion>(&value);
            if (suggestion == nullptr) return bug();
            return std::any(convert::destination(*suggestion));
        }
        case api::kInstallInstall: {
            const auto* outcome = std::any_cast<builds::InstallOutcome>(&value);
            if (outcome == nullptr) return bug();
            if (const auto* installed = std::get_if<builds::InstalledBuild>(outcome))
                return std::any(api::InstallInstallResponse{convert::build(*installed)});
            const auto& unregistered = std::get<builds::UnregisteredInstall>(*outcome);
            return make_diag(ErrorDomain::Engine, msg::kInstallNotRegistered)
                .arg("folder", unregistered.folder)
                .cause(unregistered.reason)
                .fail();
        }
        case api::kIntegrationApply:
        case api::kIntegrationRemove: {
            const auto* entries = std::any_cast<std::vector<integration::EntryStatus>>(&value);
            if (entries == nullptr) return bug();
            std::vector<api::IntegrationStatusEntry> items;
            for (const integration::EntryStatus& entry : *entries) items.push_back(convert::integration_entry(entry));
            if (method_id == api::kIntegrationApply) return std::any(api::IntegrationApplyResponse{std::move(items)});
            return std::any(api::IntegrationRemoveResponse{std::move(items)});
        }
        case api::kIntegrationRemediate: {
            const auto* prerequisite = std::any_cast<integration::Prerequisite>(&value);
            if (prerequisite == nullptr) return bug();
            return std::any(api::IntegrationRemediateResponse{convert::prerequisite(*prerequisite)});
        }
        case api::kIntegrationPurge: return std::any(api::IntegrationPurgeResponse{});
        case api::kJoinResolveLink: {
            const auto* resolution = std::any_cast<browser::LinkResolution>(&value);
            if (resolution == nullptr) return bug();
            return std::any(api::JoinResolveLinkResponse{requests::server_entry(deps, resolution->server.row),
                                                         resolution->server.row.hidden});
        }
        case api::kJoinGrant: {
            const auto* joined = std::any_cast<browser::JoinOutcome>(&value);
            if (joined == nullptr) return bug();
            return std::any(api::JoinGrantResponse{requests::server_entry(deps, joined->server.row),
                                                   joined->endpoint.to_string()});
        }
        case api::kLibraryRelocate: {
            const auto* build = std::any_cast<builds::InstalledBuild>(&value);
            if (build == nullptr) return bug();
            return std::any(api::LibraryRelocateResponse{convert::build(*build)});
        }
        case api::kLibraryImport: {
            const auto* outcome = std::any_cast<builds::ImportOutcome>(&value);
            if (outcome == nullptr) return bug();
            if (const auto* imported = std::get_if<builds::Imported>(outcome))
                return std::any(api::LibraryImportResponse{convert::build(imported->build)});
            if (const auto* choice = std::get_if<builds::NeedsShippingChoice>(outcome))
                return make_diag(ErrorDomain::Engine, msg::kImportNeedsShippingChoice)
                    .arg("folder", choice->folder)
                    .arg("count", choice->candidates.size())
                    .kind(ErrorKind::InvalidInput)
                    .fail();
            Diagnostic needs = convert::invalid_request("version");
            needs.causes = std::get<builds::NeedsUserVersion>(*outcome).reasons;
            return std::unexpected(std::move(needs));
        }
        case api::kLogsExport: {
            const auto* exported = std::any_cast<logging::LogExportResult>(&value);
            if (exported == nullptr) return bug();
            return std::any(api::LogsExportResponse{convert::path(exported->archive), exported->bytes});
        }
        case api::kUpdatesCheck: {
            const auto* offer = std::any_cast<std::optional<updates::UpdateOffer>>(&value);
            if (offer == nullptr) return bug();
            api::UpdatesCheckResponse response;
            if (*offer) response.available = convert::update_info(**offer);
            return std::any(std::move(response));
        }
        case api::kUpdatesApply: return std::any(api::UpdatesApplyResponse{});
        default: return value;
    }
}

[[nodiscard]] api::Outcome outcome_message(const ApiRouterDeps& deps, OpId op, u32 method_id, const ErasedOutcome& outcome) {
    api::Outcome out;
    out.op_id = op.value;
    out.method_id = method_id;
    std::visit(
        [&]<class A>(const A& alternative) {
            if constexpr (std::is_same_v<A, Completed<std::any>>) {
                if (method_id == 0) {
                    out.completed = api::Bytes{};
                    return;
                }
                Result<std::any> response = response_for(deps, method_id, alternative.value);
                Result<api::Bytes> bytes = response ? api::encode_op_result(method_id, *response)
                                                    : Result<api::Bytes>(std::unexpected(std::move(response.error())));
                if (bytes) out.completed = std::move(*bytes);
                else out.failed = convert::diagnostic(bytes.error());
            } else if constexpr (std::is_same_v<A, Failed>) {
                out.failed = convert::diagnostic(alternative.error);
            } else if constexpr (std::is_same_v<A, Cancelled>) {
                out.cancelled = convert::cancel_reason(alternative.reason);
            } else {
                out.timed_out = alternative.phase;
            }
        },
        outcome);
    return out;
}

}  // namespace

contracts::ipc::Bytes ApiRouter::encode_outcome(OpId op, const ErasedOutcome& outcome) {
    return api::encode(outcome_message(deps_, op, method_of(op), outcome));
}

Result<EventFilter> ApiRouter::decode_filter(std::span<const u8> filter) {
    auto decoded = api::decode<api::EventFilter>(filter);
    if (!decoded) return std::unexpected(api::to_diagnostic(decoded.error(), 0));
    EventFilter out;
    for (const api::EventKind kind : decoded->kinds)
        if (const std::optional<EventKind> domain = convert::event_kind(kind)) out.kinds.push_back(*domain);
    // Only kinds this build does not know were asked for: nothing would ever match.
    if (out.kinds.empty() && !decoded->kinds.empty()) out.kinds.push_back(EventKind::ForegroundHint);
    if (decoded->session) out.session = convert::id(*decoded->session);
    if (decoded->op_id) out.op = OpId{*decoded->op_id};
    return out;
}

std::optional<contracts::ipc::WireEvent> ApiRouter::encode_event(const EventEnvelope& event) {
    const std::optional<api::EventKind> kind = convert::event_kind(event.kind);
    if (!kind) return std::nullopt;
    api::EventPayload payload;
    const std::any& any = event.payload;
    bool known = true;
    switch (event.kind) {
        case EventKind::OpProgress:
            if (const auto* p = std::any_cast<OpProgressEvent>(&any)) payload.op_progress = convert::progress(*p, method_of(p->op));
            else known = false;
            break;
        case EventKind::OpCompleted:
            if (const auto* p = std::any_cast<OpCompletedEvent>(&any))
                payload.op_completed = outcome_message(deps_, p->op, method_of(p->op), p->outcome);
            else known = false;
            break;
        case EventKind::EngineState:
            if (const auto* p = std::any_cast<EngineStateEvent>(&any)) payload.engine_state = convert::engine_state(*p);
            else known = false;
            break;
        case EventKind::SettingsChanged:
            if (const auto* p = std::any_cast<storage::SettingsChanged>(&any))
                payload.settings_changed = api::SettingsChanged{p->revision, p->keys};
            else known = false;
            break;
        case EventKind::LibraryChanged:
            if (const auto* p = std::any_cast<builds::LibraryChangedEvent>(&any)) {
                api::LibraryChanged changed;
                changed.change = static_cast<api::LibraryChange>(std::to_underlying(p->change));
                if (p->build) changed.build = convert::id(*p->build);
                payload.library_changed = std::move(changed);
            } else {
                known = false;
            }
            break;
        case EventKind::CatalogChanged:
            if (const auto* p = std::any_cast<catalog::CatalogUpdated>(&any))
                payload.catalog_changed = api::CatalogChanged{p->serial, convert::catalog_source(p->origin)};
            else known = false;
            break;
        case EventKind::ComponentChanged:
            if (const auto* p = std::any_cast<components::ComponentChangedEvent>(&any)) {
                std::vector<components::ComponentInfo> versions;
                for (const components::ComponentInfo& info : deps_.components.list())
                    if (info.ref.id == p->info.ref.id) versions.push_back(info);
                if (versions.empty()) versions.push_back(p->info);
                std::optional<components::ComponentProblem> problem = p->problem;
                if (problem && problem->recovery == components::RecoveryState::Recovered) problem.reset();
                payload.component_changed = api::ComponentChanged{convert::component(versions, problem)};
            } else {
                known = false;
            }
            break;
        case EventKind::IdentityChanged:
            if (const auto* p = std::any_cast<identity::IdentityChangedEvent>(&any))
                payload.identity_changed = api::IdentityChanged{convert::identity_profile(p->record)};
            else known = false;
            break;
        case EventKind::SessionStateChanged:
            if (const auto* p = std::any_cast<sessions::SessionStateChanged>(&any))
                payload.session_state_changed = api::SessionStateChanged{convert::session_summary(*p)};
            else known = false;
            break;
        case EventKind::SessionSpawned:
            if (const auto* p = std::any_cast<sessions::SessionSpawned>(&any))
                payload.session_spawned = api::SessionSpawned{convert::process_role(p->process.role), p->process.pid};
            else known = false;
            break;
        case EventKind::SessionDegraded:
            if (const auto* p = std::any_cast<sessions::SessionDegraded>(&any))
                payload.session_degraded = api::SessionDegraded{convert::diagnostic(p->condition)};
            else known = false;
            break;
        case EventKind::SessionEnded:
            if (const auto* p = std::any_cast<sessions::SessionEnded>(&any)) {
                api::SessionEnded ended;
                ended.reason = convert::end_reason(p->reason);
                ended.exit_code = p->exit_code;
                if (p->error) ended.error = convert::diagnostic(*p->error);
                payload.session_ended = std::move(ended);
            } else {
                known = false;
            }
            break;
        case EventKind::HostPhaseChanged:
            if (const auto* p = std::any_cast<host::HostPhaseChanged>(&any)) payload.host_phase_changed = convert::host_phase(*p);
            else known = false;
            break;
        case EventKind::HostListening:
            if (const auto* p = std::any_cast<host::HostListening>(&any)) payload.host_listening = convert::host_listening(*p);
            else known = false;
            break;
        case EventKind::ReachabilityChanged:
            if (const auto* p = std::any_cast<publish::ReachabilityChanged>(&any))
                payload.reachability_changed = convert::reachability(*p);
            else known = false;
            break;
        case EventKind::PortMappingChanged:
            if (const auto* p = std::any_cast<net::PortMappingChanged>(&any))
                payload.port_mapping_changed = convert::port_mapping(p->mappings, p->failure);
            else known = false;
            break;
        case EventKind::PublishStateChanged:
            if (const auto* p = std::any_cast<publish::PublishStateChanged>(&any))
                payload.publish_state_changed = convert::publish_state(p->state);
            else known = false;
            break;
        case EventKind::MatchEvent:
            if (const auto* p = std::any_cast<host::MatchEvent>(&any)) payload.match_event = convert::match_event(*p);
            else known = false;
            break;
        case EventKind::PlayerEvent:
            if (const auto* p = std::any_cast<host::PlayerEvent>(&any)) payload.player_event = convert::player_event(*p);
            else known = false;
            break;
        case EventKind::BackendStateChanged:
            if (const auto* p = std::any_cast<backend::BackendEvent>(&any)) {
                api::BackendStateChanged changed;
                changed.state = convert::backend_state(p->state.phase);
                if (p->state.upstream && p->state.upstream->http_port) changed.http_port = *p->state.upstream->http_port;
                if (p->state.last_error) changed.error = convert::diagnostic(*p->state.last_error);
                payload.backend_state_changed = std::move(changed);
            } else {
                known = false;
            }
            break;
        case EventKind::XmppUnavailable:
            if (std::any_cast<front::XmppUnavailable>(&any) != nullptr)
                payload.xmpp_unavailable = api::XmppUnavailable{api::XmppUnavailableReason::ThirdPartyAuthDll};
            else known = false;
            break;
        case EventKind::BrowserConnectionChanged:
            if (const auto* p = std::any_cast<browser::ConnectionStatus>(&any)) {
                api::BrowserConnectionChanged changed;
                changed.state = convert::connection_state(p->state);
                if (p->next_attempt) {
                    const auto wait = std::chrono::duration_cast<std::chrono::milliseconds>(*p->next_attempt -
                                                                                            deps_.clock.steady_now());
                    changed.retry_in_ms = static_cast<u32>(std::clamp<i64>(wait.count(), 0, 0x7FFFFFFF));
                }
                if (p->error) changed.error = convert::diagnostic(*p->error);
                payload.browser_connection_changed = std::move(changed);
            } else {
                known = false;
            }
            break;
        case EventKind::ViewSnapshot:
        case EventKind::ViewDelta:
            if (const auto* p = std::any_cast<browser::ViewUpdate>(&any)) {
                const auto view = state_->views.find(p->view.value);
                // Views another engine client opened, and closed ones, are no one's business here.
                if (view == state_->views.end()) return std::nullopt;
                const std::string lowered = lowercase(view->second.text);
                std::vector<api::ServerEntry> rows;
                for (const browser::ServerRow& row : p->rows)
                    if (matches_text(row, lowered)) rows.push_back(requests::server_entry(deps_, row));
                const u32 total = lowered.empty() ? p->total : static_cast<u32>(rows.size());
                if (event.kind == EventKind::ViewSnapshot)
                    payload.view_snapshot = api::ViewSnapshot{p->view.value, total, 0, std::move(rows),
                                                              p->state == browser::ListState::Stale};
                else
                    payload.view_delta = api::ViewDelta{p->view.value, total, 0, std::move(rows)};
            } else {
                known = false;
            }
            break;
        case EventKind::JoinTargetChanged:
            if (const auto* p = std::any_cast<browser::JoinTargetChanged>(&any)) {
                api::JoinTargetChanged changed;
                if (p->target) changed.target = convert::join_target(*p->target);
                payload.join_target_changed = std::move(changed);
            } else {
                known = false;
            }
            break;
        case EventKind::UpdateAvailable:
            if (const auto* p = std::any_cast<updates::UpdateAvailable>(&any))
                payload.update_available = api::UpdateAvailable{convert::update_info(p->offer)};
            else known = false;
            break;
        case EventKind::UpdateStaged:
            if (const auto* p = std::any_cast<updates::UpdateStaged>(&any))
                payload.update_staged = api::UpdateStaged{convert::update_info(updates::UpdateOffer{p->update, false})};
            else known = false;
            break;
        case EventKind::EngineUpdating:
            if (const auto* p = std::any_cast<updates::EngineUpdating>(&any))
                payload.engine_updating = api::EngineUpdating{convert::semver(p->version)};
            else known = false;
            break;
        case EventKind::UpdateFailed:
            if (const auto* p = std::any_cast<updates::UpdateFailed>(&any))
                payload.update_failed = api::UpdateFailed{convert::diagnostic(p->error)};
            else known = false;
            break;
        case EventKind::NoticeAdded:
            if (const auto* p = std::any_cast<ux::NoticeAddedEvent>(&any))
                payload.notice_added = api::NoticeAdded{convert::notice(p->notice)};
            else if (const auto* notice = std::any_cast<api::Notice>(&any))
                payload.notice_added = api::NoticeAdded{*notice};
            else known = false;
            break;
        case EventKind::LogLine:
            if (const auto* p = std::any_cast<logging::LogLineEvent>(&any)) payload.log_line = log_line(*p);
            else known = false;
            break;
        case EventKind::UserActionRequired:
            if (const auto* p = std::any_cast<UserRequest>(&any))
                payload.user_action_required = requests::user_action(deps_, *p);
            else known = false;
            break;
        case EventKind::UserActionResolved:
            if (const auto* p = std::any_cast<UserActionResolvedEvent>(&any))
                payload.user_action_resolved = api::UserActionResolved{
                    p->id.value, p->resolution == RequestResolution::Answered ? api::RequestResolution::Answered
                                                                              : api::RequestResolution::Withdrawn};
            else known = false;
            break;
        case EventKind::OpStarted:
            if (const auto* p = std::any_cast<StartedOp>(&any)) {
                api::OperationSummary summary;
                summary.op_id = p->op.value;
                summary.method_id = p->method_id;
                summary.request = p->request;
                summary.detached = p->disconnect == DisconnectPolicy::Detached;
                if (p->session) summary.session = convert::id(*p->session);
                summary.started_unix_ms = convert::unix_ms(p->started_at);
                payload.op_started = api::OpStarted{std::move(summary)};
            } else {
                known = false;
            }
            break;
        case EventKind::HostProfilesChanged:
            if (const auto* p = std::any_cast<host::HostProfilesChanged>(&any)) {
                api::HostProfilesChanged changed;
                changed.change = static_cast<api::HostProfileChange>(std::to_underlying(p->change));
                if (p->profile) changed.profile = convert::id(*p->profile);
                payload.host_profiles_changed = std::move(changed);
            } else {
                known = false;
            }
            break;
        case EventKind::BackendAccountsChanged:
            if (const auto* p = std::any_cast<backend::BackendAccountsChanged>(&any)) {
                api::BackendAccountsChanged changed;
                for (const backend::BackendAccount& account : p->accounts)
                    changed.accounts.push_back(convert::backend_account(account));
                payload.backend_accounts_changed = std::move(changed);
            } else {
                known = false;
            }
            break;
        case EventKind::NoticeRemoved:
            if (const auto* p = std::any_cast<ux::NoticeRemovedEvent>(&any))
                payload.notice_removed = api::NoticeRemoved{convert::notice_key(p->key)};
            else if (const auto* key = std::any_cast<api::NoticeKey>(&any))
                payload.notice_removed = api::NoticeRemoved{*key};
            else known = false;
            break;
        case EventKind::OnboardingChanged:
            if (const auto* p = std::any_cast<ux::OnboardingChangedEvent>(&any))
                payload.onboarding_changed = api::OnboardingChanged{convert::onboarding(p->view)};
            else known = false;
            break;
        case EventKind::IntegrationChanged:
            if (const auto* p = std::any_cast<integration::IntegrationChanged>(&any)) {
                api::IntegrationChanged changed;
                for (const integration::EntryStatus& entry : p->entries) changed.items.push_back(convert::integration_entry(entry));
                payload.integration_changed = std::move(changed);
            } else {
                known = false;
            }
            break;
        case EventKind::PrerequisitesChanged:
            if (const auto* p = std::any_cast<integration::PrerequisitesChanged>(&any)) {
                api::PrerequisitesChanged changed;
                for (const integration::Prerequisite& entry : p->prerequisites)
                    changed.prerequisites.push_back(convert::prerequisite(entry));
                payload.prerequisites_changed = std::move(changed);
            } else {
                known = false;
            }
            break;
        case EventKind::SecretStateChanged:
            if (const auto* p = std::any_cast<secrets::SecretStateChangedEvent>(&any)) {
                api::SecretStateChanged changed;
                changed.target = convert::secret_target(p->target);
                changed.present = p->state.present();
                changed.store = convert::secret_store(p->state.location);
                payload.secret_state_changed = std::move(changed);
            } else {
                known = false;
            }
            break;
        case EventKind::StorageModeChanged:
            if (const auto* p = std::any_cast<storage::StorageModeChanged>(&any))
                payload.storage_mode_changed = convert::storage_mode_changed(*p);
            else known = false;
            break;
        default: known = false; break;
    }
    if (!known) return std::nullopt;

    contracts::ipc::WireEvent out;
    out.kind = static_cast<u32>(std::to_underlying(*kind));
    out.epoch = event.epoch.value;
    out.seq = event.seq;
    if (event.session) out.session = event.session->value;
    if (event.op) out.op = event.op->value;
    out.payload = api::encode(payload);
    return out;
}

Result<void> ApiRouter::put_secret(const ipc::ConnectionInfo&, std::span<const u8> target, SecretBytes secret) {
    auto decoded = api::decode<api::SecretTarget>(target);
    if (!decoded) return std::unexpected(api::to_diagnostic(decoded.error(), 0));
    Result<secrets::SecretTarget> parsed = convert::secret_target(*decoded);
    if (!parsed) return std::unexpected(std::move(parsed.error()));
    const secrets::SecretTarget held = *parsed;
    Result<void> put = deps_.secrets.put(*parsed, std::move(secret), std::nullopt,
                                         [held](Result<secrets::SecretState> saved) {
                                             if (!saved)
                                                 REBOOT_LOG_WARN(Engine, "a {} secret was not saved: {}",
                                                                 secrets::kind_name(held.kind), saved.error().id);
                                         });
    if (!put) return put;
    if (held.kind == secrets::SecretKind::HostJoinPassword && decoded->host_profile)
        if (Result<void> refreshed = deps_.hosts.refresh_join_password(convert::id(*decoded->host_profile)); !refreshed)
            REBOOT_LOG_WARN(Host, "live sessions keep their join password: {}", refreshed.error().id);
    return {};
}

Result<SecretBytes> ApiRouter::reveal_secret(const ipc::ConnectionInfo&, std::span<const u8> target) {
    auto decoded = api::decode<api::SecretTarget>(target);
    if (!decoded) return std::unexpected(api::to_diagnostic(decoded.error(), 0));
    Result<secrets::SecretTarget> parsed = convert::secret_target(*decoded);
    if (!parsed) return std::unexpected(std::move(parsed.error()));
    return deps_.secrets.reveal(*parsed);
}

}  // namespace rb::engine
