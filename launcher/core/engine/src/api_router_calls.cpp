// The generated handlers: each turns its request into service calls and the result into its response.
#include <algorithm>
#include <cctype>
#include <format>
#include <limits>
#include <string>
#include <utility>
#include <vector>

#include <boost/json/parse.hpp>
#include <boost/json/serialize.hpp>
#include <boost/system/error_code.hpp>

#include "api_convert.hpp"
#include "api_requests.hpp"
#include "api_router_state.hpp"
#include "host_platform.hpp"
#include "messages.hpp"
#include "reboot/api/v1/method_table.hpp"
#include "reboot/backend/account_prune_filter.hpp"
#include "reboot/backend/account_rename_request.hpp"
#include "reboot/backend/backend_accounts.hpp"
#include "reboot/backend/backend_config.hpp"
#include "reboot/backend/backend_service.hpp"
#include "reboot/backend/console_key.hpp"
#include "reboot/backend/hid_usage.hpp"
#include "reboot/browser/browser_session.hpp"
#include "reboot/browser/deep_link_service.hpp"
#include "reboot/browser/game_server_target.hpp"
#include "reboot/browser/join_service.hpp"
#include "reboot/browser/join_outcome.hpp"
#include "reboot/browser/own_servers.hpp"
#include "reboot/browser/rbsb_request_error.hpp"
#include "reboot/browser/server_row.hpp"
#include "reboot/browser/server_list.hpp"
#include "reboot/builds/build_installer.hpp"
#include "reboot/builds/import_service.hpp"
#include "reboot/builds/install_request.hpp"
#include "reboot/builds/library.hpp"
#include "reboot/catalog/catalog_service.hpp"
#include "reboot/compat/runtime_service.hpp"
#include "reboot/components/component_store.hpp"
#include "reboot/components/manifest_platform.hpp"
#include "reboot/engine/engine_info.hpp"
#include "reboot/engine/engine_lifecycle.hpp"
#include "reboot/foundation/executor.hpp"
#include "reboot/foundation/log.hpp"
#include "reboot/foundation/paths.hpp"
#include "reboot/foundation/text.hpp"
#include "reboot/foundation/user_request.hpp"
#include "reboot/gameserver/game_server_binary.hpp"
#include "reboot/host/host_command.hpp"
#include "reboot/host/host_error.hpp"
#include "reboot/host/host_service.hpp"
#include "reboot/host/host_start_request.hpp"
#include "reboot/identity/identity_service.hpp"
#include "reboot/integration/integration_kind.hpp"
#include "reboot/integration/integration_service.hpp"
#include "reboot/integration/openable_url.hpp"
#include "reboot/integration/prerequisite_error.hpp"
#include "reboot/integration/prerequisite_service.hpp"
#include "reboot/integration/purge_service.hpp"
#include "reboot/integration/shell_service.hpp"
#include "reboot/logging/error_router.hpp"
#include "reboot/logging/log_exporter.hpp"
#include "reboot/logging/log_filter.hpp"
#include "reboot/logging/log_ring.hpp"
#include "reboot/play/play_plan.hpp"
#include "reboot/play/play_request.hpp"
#include "reboot/play/play_service.hpp"
#include "reboot/publish/host_identity_store.hpp"
#include "reboot/secrets/secret_service.hpp"
#include "reboot/sessions/session_phase.hpp"
#include "reboot/sessions/session_registry.hpp"
#include "reboot/sessions/stop_request.hpp"
#include "reboot/storage/frontend_state_store.hpp"
#include "reboot/storage/json_values.hpp"
#include "reboot/storage/reset_service.hpp"
#include "reboot/storage/settings.hpp"
#include "reboot/storage/settings_patch.hpp"
#include "reboot/storage/settings_registry.hpp"
#include "reboot/storage/shell_name.hpp"
#include "reboot/support/host_inputs.hpp"
#include "reboot/support/support_policy.hpp"
#include "reboot/updates/update_service.hpp"
#include "reboot/ux/app_links.hpp"
#include "reboot/ux/doc_page.hpp"
#include "reboot/ux/language_info.hpp"
#include "reboot/ux/language_preference.hpp"
#include "reboot/ux/notice_service.hpp"
#include "reboot/ux/onboarding.hpp"
#include "reboot/ux/resolve_language.hpp"
#include "reboot/ux/settings_search.hpp"

namespace rb::engine {

namespace json = boost::json;

namespace {

constexpr std::size_t kSearchLimit = 50;
constexpr u32 kDefaultLogPage = 500;
constexpr u32 kMaxLogPage = 5000;

[[nodiscard]] Diagnostic not_ready(std::string_view what) {
    return make_diag(ErrorDomain::Engine, msg::kNotReady).arg("what", what).retryable().kind(ErrorKind::Conflict);
}

[[nodiscard]] Result<NativePath> absolute_path(const api::Path& wire, std::string_view field) {
    Result<NativePath> path = convert::path(wire, field);
    if (path && !path->is_absolute()) return std::unexpected(convert::invalid_request(field));
    return path;
}

[[nodiscard]] builds::RunningPolicy build_policy(api::RunningPolicy policy) {
    return policy == api::RunningPolicy::StopSessions ? builds::RunningPolicy::StopSessions : builds::RunningPolicy::Refuse;
}

[[nodiscard]] std::optional<storage::ResetGroup> reset_group(api::SettingGroup group) {
    switch (group) {
        case api::SettingGroup::Play: return storage::ResetGroup::Play;
        case api::SettingGroup::Host: return storage::ResetGroup::Host;
        case api::SettingGroup::Backend: return storage::ResetGroup::Backend;
        case api::SettingGroup::App: break;
    }
    return std::nullopt;
}

[[nodiscard]] api::SettingValue setting_value(storage::ValueKind kind, const json::value& value) {
    api::SettingValue out;
    switch (kind) {
        case storage::ValueKind::Bool: out.flag = value.is_bool() && value.get_bool(); break;
        case storage::ValueKind::OptionalPath:
            if (Result<NativePath> path = storage::path_from_json(value); !value.is_null() && path) out.path = convert::path(*path);
            else out.path = api::Path{};
            break;
        case storage::ValueKind::BackendTarget: out.text = json::serialize(value); break;
        case storage::ValueKind::Text:
        case storage::ValueKind::Choice:
        case storage::ValueKind::ConsoleKey:
            out.text = value.is_string() ? std::string(value.get_string()) : json::serialize(value);
            break;
    }
    return out;
}

[[nodiscard]] Result<json::value> setting_json(storage::ValueKind kind, const api::SettingValue& value, std::string_view field) {
    switch (kind) {
        case storage::ValueKind::Bool:
            if (value.flag) return json::value(*value.flag);
            break;
        case storage::ValueKind::OptionalPath:
            if (value.path) {
                if (value.path->native.empty()) return json::value(nullptr);
                Result<NativePath> path = convert::path(*value.path, field);
                if (!path) return std::unexpected(std::move(path.error()));
                return storage::path_to_json(*path);
            }
            break;
        case storage::ValueKind::BackendTarget:
            if (value.text) {
                boost::system::error_code error;
                json::value parsed = json::parse(*value.text, error);
                if (!error) return parsed;
            }
            break;
        case storage::ValueKind::Text:
        case storage::ValueKind::Choice:
        case storage::ValueKind::ConsoleKey:
            if (value.text) return json::value(json::string(*value.text));
            break;
    }
    return std::unexpected(convert::invalid_request(field));
}

// Every field of `next` that differs from `current`.
[[nodiscard]] storage::SettingsPatch diff(const storage::SettingsValues& current, const storage::SettingsValues& next) {
    storage::SettingsPatch patch;
    if (next.client.game_culture != current.client.game_culture) patch.client.game_culture = next.client.game_culture;
    if (next.play.custom_args != current.play.custom_args) patch.play.custom_args = next.play.custom_args;
    if (next.play.custom_auth_dll != current.play.custom_auth_dll) patch.play.custom_auth_dll = next.play.custom_auth_dll;
    if (next.play.env != current.play.env) patch.play.env = next.play.env;
    if (next.play.verbose_wine_log != current.play.verbose_wine_log) patch.play.verbose_wine_log = next.play.verbose_wine_log;
    if (!(next.backend.target == current.backend.target)) patch.backend.target = next.backend.target;
    if (next.backend.allow_lan != current.backend.allow_lan) patch.backend.allow_lan = next.backend.allow_lan;
    if (!(next.backend.console_key == current.backend.console_key)) patch.backend.console_key = next.backend.console_key;
    if (next.host.update_policy != current.host.update_policy) patch.host.update_policy = next.host.update_policy;
    if (next.host.listing != current.host.listing) patch.host.listing = next.host.listing;
    if (next.updates.channel != current.updates.channel) patch.updates.channel = next.updates.channel;
    if (next.updates.auto_check != current.updates.auto_check) patch.updates.auto_check = next.updates.auto_check;
    if (next.ui.language != current.ui.language) patch.ui.language = next.ui.language;
    if (next.ui.theme != current.ui.theme) patch.ui.theme = next.ui.theme;
    return patch;
}

[[nodiscard]] ux::LanguageTag ui_language(const storage::Settings& settings) {
    const std::string selected = settings.snapshot().values.ui.language;
    ux::LanguagePreference preference;
    if (Result<ux::LanguagePreference> parsed = ux::LanguagePreference::parse(selected)) preference = *parsed;
    return ux::resolve_language(preference, {}, ux::shipped_languages()).tag;
}

[[nodiscard]] Result<host::HostCommand> host_command(const api::OperatorCommand& command) {
    if (command.start_match) return host::HostCommand{gameserver::StartMatch{std::chrono::seconds{command.start_match->countdown_s}}};
    if (command.end_match) return host::HostCommand{gameserver::EndMatch{}};
    if (command.reset) return host::HostCommand{gameserver::ResetMatch{}};
    if (command.kick) return host::HostCommand{gameserver::Kick{command.kick->player_id, command.kick->reason}};
    if (command.set_bans) {
        host::ReplaceBans bans;
        for (const api::Ban& ban : command.set_bans->bans) {
            Result<host::HostBan> parsed = convert::host_ban(ban);
            if (!parsed) return std::unexpected(std::move(parsed.error()));
            bans.bans.push_back(std::move(*parsed));
        }
        return host::HostCommand{std::move(bans)};
    }
    if (command.set_operators) {
        host::ReplaceOperators operators;
        for (const std::string& text : command.set_operators->cidrs) {
            Result<host::IpCidr> cidr = host::IpCidr::parse(text);
            if (!cidr) return std::unexpected(std::move(cidr.error()));
            operators.operator_cidrs.push_back(*cidr);
        }
        return host::HostCommand{std::move(operators)};
    }
    if (command.console) return host::HostCommand{gameserver::RunCommand{command.console->text}};
    return std::unexpected(convert::invalid_request("command"));
}

[[nodiscard]] Result<std::vector<ports::IntegrationKind>> integration_kinds(const std::vector<api::IntegrationItem>& items) {
    std::vector<ports::IntegrationKind> kinds;
    for (const api::IntegrationItem item : items) {
        if (std::to_underlying(item) > std::to_underlying(api::IntegrationItem::DesktopEntry))
            return std::unexpected(convert::invalid_request("items"));
        kinds.push_back(static_cast<ports::IntegrationKind>(std::to_underlying(item)));
    }
    return kinds;
}

[[nodiscard]] std::string_view trim(std::string_view text) {
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front())) != 0) text.remove_prefix(1);
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back())) != 0) text.remove_suffix(1);
    return text;
}

[[nodiscard]] std::string lowercase(std::string_view text) {
    std::string out(text);
    for (char& c : out) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return out;
}

void log_failure(std::string_view what, const Result<void>& result) {
    if (!result) REBOOT_LOG_WARN(Engine, "{} failed: {}", what, result.error().id);
}

}  // namespace

// Backend

Result<api::BackendStatusResponse> ApiRouter::status(const api::CallContext&, const api::BackendStatusRequest&) {
    const backend::BackendState& state = deps_.backend.state();
    api::BackendStatusResponse out;
    out.target = convert::backend_target(state.config.target);
    out.state = convert::backend_state(state.phase);
    out.pinned = state.pinned;
    out.leases = state.leases;
    if (state.upstream && state.upstream->http_port) out.http_port = *state.upstream->http_port;
    out.version = state.version;
    out.allow_lan = state.config.allow_lan;
    if (state.last_error) out.last_error = convert::diagnostic(*state.last_error);
    return out;
}

Result<api::BackendDataDirResponse> ApiRouter::data_dir(const api::CallContext&, const api::BackendDataDirRequest&) {
    return api::BackendDataDirResponse{convert::path(deps_.layout.backend_dir())};
}

Result<api::BackendSetTargetResponse> ApiRouter::set_target(const api::CallContext&, const api::BackendSetTargetRequest& request) {
    Result<backend::BackendTarget> target = convert::backend_target(request.target);
    if (!target) return std::unexpected(std::move(target.error()));
    backend::BackendConfig config = deps_.backend.state().config;
    config.target = *target;
    const backend::RunningPolicy policy = request.running_policy == api::RunningPolicy::StopSessions
                                              ? backend::RunningPolicy::StopSessions
                                              : backend::RunningPolicy::Refuse;
    if (Result<backend::ReconfigureTiming> applied = deps_.backend.reconfigure(config, policy); !applied)
        return std::unexpected(std::move(applied.error()));
    storage::SettingsPatch patch;
    storage::BackendTarget stored = deps_.settings.snapshot().values.backend.target;
    target->apply_to(stored);
    patch.backend.target = std::move(stored);
    if (Result<u64> written = deps_.settings.patch(patch); !written) return std::unexpected(std::move(written.error()));
    return api::BackendSetTargetResponse{};
}

Result<api::BackendAccountsListResponse> ApiRouter::accounts_list(const api::CallContext&, const api::BackendAccountsListRequest&) {
    Result<std::vector<backend::BackendAccount>> accounts = deps_.backend_accounts.list();
    if (!accounts) return std::unexpected(std::move(accounts.error()));
    api::BackendAccountsListResponse out;
    for (const backend::BackendAccount& account : *accounts) out.accounts.push_back(convert::backend_account(account));
    return out;
}

Result<OpHandle> ApiRouter::start(const api::CallContext&, const api::BackendStartRequest& request, DisconnectPolicy disconnect) {
    return deps_.backend.start_backend(request.pin, disconnect);
}

Result<OpHandle> ApiRouter::start_stop(const api::CallContext&, const api::BackendStopRequest&, DisconnectPolicy disconnect) {
    return deps_.backend.start_stop(disconnect);
}

Result<OpHandle> ApiRouter::start_accounts_reset(const api::CallContext&, const api::BackendAccountsResetRequest& request,
                                                 DisconnectPolicy disconnect) {
    return deps_.backend_accounts.start_reset(request.account_id, disconnect);
}

Result<OpHandle> ApiRouter::start_accounts_delete(const api::CallContext&, const api::BackendAccountsDeleteRequest& request,
                                                  DisconnectPolicy disconnect) {
    return deps_.backend_accounts.start_delete(request.account_id, disconnect);
}

Result<OpHandle> ApiRouter::start_accounts_prune(const api::CallContext&, const api::BackendAccountsPruneRequest& request,
                                                 DisconnectPolicy disconnect) {
    backend::AccountPruneFilter filter;
    filter.last_login_before = deps_.clock.system_now() - std::chrono::days{request.older_than_days};
    filter.role = static_cast<contracts::backend::AccountRole>(std::to_underlying(request.kind));
    return deps_.backend_accounts.start_prune(filter, disconnect);
}

Result<OpHandle> ApiRouter::start_accounts_rename(const api::CallContext&, const api::BackendAccountsRenameRequest& request,
                                                  DisconnectPolicy disconnect) {
    backend::AccountRenameRequest rename;
    rename.old_account_id = request.account_id;
    rename.new_account_id = request.display_name;
    switch (request.on_conflict) {
        case api::RenameConflict::Ask: rename.on_conflict = backend::RenameConflictChoice::Ask; break;
        case api::RenameConflict::KeepExisting: rename.on_conflict = backend::RenameConflictChoice::KeepExisting; break;
        case api::RenameConflict::Replace: rename.on_conflict = backend::RenameConflictChoice::Replace; break;
    }
    return deps_.backend_accounts.start_rename(std::move(rename), disconnect);
}

// Browser

Result<api::BrowserOpenViewResponse> ApiRouter::open_view(const api::CallContext& context, const api::BrowserOpenViewRequest& request) {
    Result<browser::ViewSpec> spec = convert::view_spec(request.spec);
    if (!spec) return std::unexpected(std::move(spec.error()));
    if (!state_->browser_lease.held()) state_->browser_lease = deps_.browser.acquire();
    Result<browser::ViewId> view = deps_.views.open(*spec);
    if (!view) {
        if (state_->views.empty()) state_->browser_lease.release();
        return std::unexpected(std::move(view.error()));
    }
    state_->views.insert_or_assign(view->value, State::View{context.connection, request.spec.text});
    return api::BrowserOpenViewResponse{view->value};
}

Result<api::BrowserUpdateViewResponse> ApiRouter::update_view(const api::CallContext&, const api::BrowserUpdateViewRequest& request) {
    const auto view = state_->views.find(request.view_id);
    if (view == state_->views.end())
        return make_diag(ErrorDomain::Engine, msg::kUnknownView).arg("view", request.view_id).kind(ErrorKind::NotFound).fail();
    Result<browser::ViewSpec> spec = convert::view_spec(request.spec);
    if (!spec) return std::unexpected(std::move(spec.error()));
    if (Result<void> updated = deps_.views.update(browser::ViewId{request.view_id}, *spec); !updated)
        return std::unexpected(std::move(updated.error()));
    view->second.text = request.spec.text;
    return api::BrowserUpdateViewResponse{};
}

Result<api::BrowserCloseViewResponse> ApiRouter::close_view(const api::CallContext&, const api::BrowserCloseViewRequest& request) {
    if (state_->views.erase(request.view_id) == 0)
        return make_diag(ErrorDomain::Engine, msg::kUnknownView).arg("view", request.view_id).kind(ErrorKind::NotFound).fail();
    deps_.views.close(browser::ViewId{request.view_id});
    if (state_->views.empty()) state_->browser_lease.release();
    return api::BrowserCloseViewResponse{};
}

Result<api::BrowserStateResponse> ApiRouter::state(const api::CallContext&, const api::BrowserStateRequest&) {
    const browser::ConnectionStatus& status = deps_.browser.status();
    api::BrowserStateResponse out;
    out.state = convert::connection_state(status.state);
    for (const auto& [view, owner] : state_->views) out.open_views.push_back(view);
    if (status.error) out.error = convert::diagnostic(*status.error);
    return out;
}

Result<OpHandle> ApiRouter::start_resolve(const api::CallContext&, const api::BrowserResolveRequest& request,
                                          DisconnectPolicy disconnect) {
    const ServerId server = convert::id(request.server);
    auto [handle, op] = deps_.ops.create<std::any>(OpKind::QuicConnect, disconnect, std::nullopt, RunnerMultiplier::Native,
                                                   std::chrono::seconds{30});
    // The browse connection only lives while a lease is held.
    auto lease = std::make_shared<browser::BrowserLease>(deps_.browser.acquire());
    deps_.browser.resolve(server, op.token(), [this, &op, server, lease, alive = state_->alive.token()](
                                                  browser::RbsbResult<sb::wire::ResolveResult> resolved) mutable {
        lease->release();
        if (!resolved) {
            op.complete(Failed{browser::to_diagnostic(resolved.error())});
            return;
        }
        if (alive.cancelled()) {
            op.complete(Cancelled{CancelReason::Shutdown});
            return;
        }
        if (!resolved->details) {
            op.complete(Failed{browser::to_diagnostic(
                browser::JoinFailure{.code = browser::JoinFailureCode::NotFound, .server = server})});
            return;
        }
        const std::chrono::milliseconds offset =
            deps_.browser.edge() ? deps_.browser.edge()->clock_offset : std::chrono::milliseconds{0};
        const browser::ServerDetails details = browser::make_server_details(*resolved->details, offset);
        op.complete(Completed<std::any>{std::any(api::BrowserResolveResponse{requests::server_entry(deps_, details.row),
                                                                             details.row.hidden})});
    });
    return handle;
}

// Catalog

Result<api::CatalogListResponse> ApiRouter::list(const api::CallContext&, const api::CatalogListRequest& request) {
    const catalog::Catalog& current = deps_.catalog.current();
    api::CatalogListResponse out;
    out.serial = current.serial;
    out.generated_unix_ms = convert::unix_ms(current.generated_at);
    out.source = convert::catalog_source(deps_.catalog.origin().value_or(catalog::CatalogOrigin::Bundled));
    out.expired = deps_.catalog.origin().has_value() && current.expires_at < deps_.clock.system_now();
    const std::string text = lowercase(request.text);
    const std::vector<builds::InstalledBuild> installed = deps_.library.list();
    for (const catalog::CatalogEntry& entry :
         deps_.catalog.list(catalog::CatalogFilter{.include_unavailable = request.include_unavailable})) {
        if (!text.empty() && lowercase(entry.id).find(text) == std::string::npos &&
            lowercase(entry.display_name).find(text) == std::string::npos)
            continue;
        const bool is_installed = std::ranges::any_of(installed, [&entry](const builds::InstalledBuild& build) {
            return build.catalog_entry == entry.id;
        });
        out.entries.push_back(convert::catalog_entry(entry, is_installed));
    }
    return out;
}

Result<OpHandle> ApiRouter::start_refresh(const api::CallContext&, const api::CatalogRefreshRequest& request,
                                          DisconnectPolicy disconnect) {
    return deps_.catalog.start_refresh(request.force ? catalog::CatalogRefresh::Force : catalog::CatalogRefresh::IfExpired,
                                       disconnect);
}

// Components

Result<api::ComponentsListResponse> ApiRouter::list(const api::CallContext&, const api::ComponentsListRequest&) {
    api::ComponentsListResponse out;
    out.components = convert::components(deps_.components.list());
    for (api::Component& component : out.components)
        if (const auto problem = state_->component_problems.find(component.id); problem != state_->component_problems.end()) {
            component.problem = convert::component_problem(problem->second);
            component.state = api::ComponentState::Broken;
        }
    return out;
}

Result<OpHandle> ApiRouter::start_ensure(const api::CallContext&, const api::ComponentsEnsureRequest& request,
                                         DisconnectPolicy disconnect) {
    return deps_.components.start_ensure(request.id, disconnect);
}

Result<OpHandle> ApiRouter::start_remove(const api::CallContext&, const api::ComponentsRemoveRequest& request,
                                         DisconnectPolicy disconnect) {
    return deps_.components.start_remove(request.id, disconnect);
}

Result<OpHandle> ApiRouter::start_runtime_setup(const api::CallContext&, const api::ComponentsRuntimeSetupRequest& request,
                                                DisconnectPolicy disconnect) {
    return deps_.runtimes.start_setup(convert::runner(request.runner), disconnect);
}

// Engine

Result<api::EngineStatusResponse> ApiRouter::status(const api::CallContext&, const api::EngineStatusRequest&) {
    api::EngineStatusResponse out;
    out.engine_build = deps_.info.build;
    out.epoch = deps_.info.epoch.value;
    out.pid = deps_.info.self.pid;
    out.origin = static_cast<api::EngineOrigin>(std::to_underlying(deps_.lifecycle.origin()));
    out.phase = convert::engine_phase(deps_.lifecycle.phase());
    for (const sessions::SessionInfo& session : deps_.sessions.list())
        if (sessions::is_live(session.phase)) ++out.sessions;
    out.operations = static_cast<u32>(deps_.ops.live().size());
    return out;
}

Result<api::EngineInfoResponse> ApiRouter::info(const api::CallContext&, const api::EngineInfoRequest&) {
    api::EngineInfoResponse out;
    out.engine_build = deps_.info.build;
    out.app_version = convert::semver(deps_.info.app_version);
    switch (kHostOs) {
        case components::ManifestOs::Windows: out.os = api::HostOs::Windows; break;
        case components::ManifestOs::MacOs: out.os = api::HostOs::Macos; break;
        case components::ManifestOs::Linux: out.os = api::HostOs::Linux; break;
    }
    out.os_version = deps_.info.os.version;
    out.image_path = convert::path(deps_.info.self.image_path);
    out.data_root = convert::path(deps_.info.canonical_root);
    out.storage_mode = static_cast<api::StorageMode>(std::to_underlying(deps_.info.storage_mode));
    out.secrets_available = deps_.info.secrets_available;
    out.schema_fingerprint = std::string(api::kSchemaFingerprint);
    return out;
}

Result<api::EngineDrainResponse> ApiRouter::drain(const api::CallContext&, const api::EngineDrainRequest& request) {
    DrainReason reason = DrainReason::UserStop;
    switch (request.reason) {
        case api::DrainReason::Update: reason = DrainReason::Update; break;
        case api::DrainReason::UserStop: reason = DrainReason::UserStop; break;
        case api::DrainReason::Replace: reason = DrainReason::Replace; break;
    }
    if (Result<void> drained = deps_.lifecycle.drain(reason); !drained) return std::unexpected(std::move(drained.error()));
    return api::EngineDrainResponse{};
}

Result<api::EngineShutdownResponse> ApiRouter::shutdown(const api::CallContext&, const api::EngineShutdownRequest& request) {
    const ShutdownWhen when = request.when == api::ShutdownWhen::Idle ? ShutdownWhen::WhenIdle : ShutdownWhen::Now;
    if (Result<void> scheduled = deps_.lifecycle.shutdown(when); !scheduled) return std::unexpected(std::move(scheduled.error()));
    return api::EngineShutdownResponse{};
}

Result<api::EngineRestartWhenIdleResponse> ApiRouter::restart_when_idle(const api::CallContext&,
                                                                        const api::EngineRestartWhenIdleRequest&) {
    if (Result<void> scheduled = deps_.lifecycle.restart_when_idle(); !scheduled)
        return std::unexpected(std::move(scheduled.error()));
    return api::EngineRestartWhenIdleResponse{};
}

Result<api::EngineOperationsResponse> ApiRouter::operations(const api::CallContext&, const api::EngineOperationsRequest&) {
    api::EngineOperationsResponse out;
    for (const LiveOp& live : deps_.ops.live()) {
        const auto started = started_ops_.find(live.op);
        if (started == started_ops_.end()) continue;
        api::OperationSummary summary;
        summary.op_id = live.op.value;
        summary.method_id = started->second.method_id;
        summary.request = started->second.request;
        summary.detached = live.policy == DisconnectPolicy::Detached;
        if (live.session) summary.session = convert::id(*live.session);
        else if (started->second.session) summary.session = convert::id(*started->second.session);
        summary.started_unix_ms = convert::unix_ms(started->second.started_at);
        if (live.progress) summary.progress = convert::progress(*live.progress, started->second.method_id);
        out.operations.push_back(std::move(summary));
    }
    return out;
}

// Guidance

Result<api::GuidanceNoticesListResponse> ApiRouter::notices_list(const api::CallContext&, const api::GuidanceNoticesListRequest&) {
    api::GuidanceNoticesListResponse out;
    for (const ux::Notice& notice : deps_.notices.list()) out.notices.push_back(convert::notice(notice));
    for (const logging::BackgroundFailure& failure : deps_.errors.background_failures())
        out.notices.push_back(convert::background_notice(failure));
    return out;
}

Result<api::GuidanceNoticesDismissResponse> ApiRouter::notices_dismiss(const api::CallContext&,
                                                                       const api::GuidanceNoticesDismissRequest& request) {
    const std::string& kind = request.key.kind;
    if (const std::optional<logging::BackgroundFailureId> failure = convert::background_failure_of(request.key)) {
        deps_.errors.acknowledge(*failure);
        return api::GuidanceNoticesDismissResponse{};
    }
    const std::optional<ux::NoticeKind> notice = ux::parse_notice_kind(kind);
    if (!notice) return make_diag(ErrorDomain::Engine, msg::kUnknownNotice).arg("kind", kind).kind(ErrorKind::NotFound).fail();
    ux::NoticeKey key{*notice, std::nullopt};
    if (request.key.session) key.session = convert::id(*request.key.session);
    if (Result<void> dismissed = deps_.notices.dismiss(key); !dismissed) return std::unexpected(std::move(dismissed.error()));
    return api::GuidanceNoticesDismissResponse{};
}

namespace {

[[nodiscard]] ux::OnboardingContext onboarding_context(const ApiRouterDeps& deps,
                                                       const std::optional<std::vector<integration::Prerequisite>>& checked) {
    ux::OnboardingContext context;
    if (checked)
        for (const integration::Prerequisite& prerequisite : *checked)
            context.prerequisites.push_back(
                ports::PrerequisiteStatus{std::string(integration::to_string(prerequisite.id)), prerequisite.met, std::nullopt});
    context.library_empty = deps.library.list().empty();
    return context;
}

}  // namespace

Result<api::GuidanceOnboardingStateResponse> ApiRouter::onboarding_state(const api::CallContext&,
                                                                         const api::GuidanceOnboardingStateRequest&) {
    return api::GuidanceOnboardingStateResponse{
        convert::onboarding(deps_.onboarding.view(onboarding_context(deps_, state_->prerequisites)))};
}

Result<api::GuidanceOnboardingAdvanceResponse> ApiRouter::onboarding_advance(const api::CallContext&,
                                                                             const api::GuidanceOnboardingAdvanceRequest& request) {
    const std::optional<ux::StepId> step = ux::parse_step_id(request.step);
    if (!step) return std::unexpected(convert::invalid_request("step"));
    std::optional<ux::OnboardingChoiceId> choice;
    if (!request.choice.empty()) {
        choice = ux::parse_choice_id(request.choice);
        if (!choice) return std::unexpected(convert::invalid_request("choice"));
    }
    const ux::OnboardingContext context = onboarding_context(deps_, state_->prerequisites);
    // The tour starts with its first step: advancing past it also starts or restarts it.
    if (deps_.onboarding.view(context).status != ux::OnboardingStatus::InProgress)
        if (Result<ux::OnboardingView> started = deps_.onboarding.start(context); !started)
            return std::unexpected(std::move(started.error()));
    Result<ux::AdvanceResult> advanced = deps_.onboarding.advance(*step, choice, context);
    if (!advanced) return std::unexpected(std::move(advanced.error()));
    api::GuidanceOnboardingAdvanceResponse out;
    out.state = convert::onboarding(advanced->view);
    if (advanced->action) out.action = convert::suggested_action(*advanced->action);
    return out;
}

Result<api::GuidanceOnboardingSkipResponse> ApiRouter::onboarding_skip(const api::CallContext&,
                                                                       const api::GuidanceOnboardingSkipRequest&) {
    Result<ux::OnboardingView> exited = deps_.onboarding.exit(onboarding_context(deps_, state_->prerequisites));
    if (!exited) return std::unexpected(std::move(exited.error()));
    return api::GuidanceOnboardingSkipResponse{convert::onboarding(*exited)};
}

Result<api::GuidanceLinksResponse> ApiRouter::links(const api::CallContext&, const api::GuidanceLinksRequest&) {
    const ux::LanguageTag language = ui_language(deps_.settings);
    api::GuidanceLinksResponse out;
    for (const ux::AppLinkEntry& entry : deps_.app_links.list(language)) {
        std::string id;
        switch (entry.link) {
            case ux::AppLink::BugReport: id = "bug_report"; break;
            case ux::AppLink::Releases: id = "releases"; break;
            case ux::AppLink::Discord: id = "discord"; break;
        }
        out.links.push_back(api::Link{std::move(id), entry.url});
    }
    out.links.push_back(api::Link{"port_forwarding", ux::resolve_doc_url(ux::DocPage::PortForwardingGuide, language)});
    return out;
}

// Host

namespace {

[[nodiscard]] bool has_join_password(secrets::SecretService& secrets, const HostProfileId& profile) {
    Result<secrets::SecretTarget> target = secrets::SecretTarget::parse(
        secrets::SecretKind::HostJoinPassword, secrets::SecretScope::host_profile(profile).text());
    if (!target) return false;
    Result<secrets::SecretState> state = secrets.state(*target);
    return state && state->present();
}

}  // namespace

Result<api::HostProfilesListResponse> ApiRouter::profiles_list(const api::CallContext&, const api::HostProfilesListRequest&) {
    api::HostProfilesListResponse out;
    for (const host::HostProfile& profile : deps_.hosts.profiles())
        out.profiles.push_back(convert::host_profile(profile, has_join_password(deps_.secrets, profile.id)));
    return out;
}

Result<api::HostProfilesCreateResponse> ApiRouter::profiles_create(const api::CallContext&,
                                                                   const api::HostProfilesCreateRequest& request) {
    Result<host::HostProfile> draft = convert::host_profile(request.profile);
    if (!draft) return std::unexpected(std::move(draft.error()));
    Result<host::HostProfile> created = deps_.hosts.create_profile(std::move(*draft));
    if (!created) return std::unexpected(std::move(created.error()));
    return api::HostProfilesCreateResponse{convert::host_profile(*created, has_join_password(deps_.secrets, created->id))};
}

Result<api::HostProfilesUpdateResponse> ApiRouter::profiles_update(const api::CallContext&,
                                                                   const api::HostProfilesUpdateRequest& request) {
    Result<host::HostProfile> profile = convert::host_profile(request.profile);
    if (!profile) return std::unexpected(std::move(profile.error()));
    Result<host::HostProfile> updated = deps_.hosts.update_profile(std::move(*profile));
    if (!updated) return std::unexpected(std::move(updated.error()));
    return api::HostProfilesUpdateResponse{convert::host_profile(*updated, has_join_password(deps_.secrets, updated->id))};
}

Result<api::HostProfilesDeleteResponse> ApiRouter::profiles_delete(const api::CallContext&,
                                                                   const api::HostProfilesDeleteRequest& request) {
    const HostProfileId profile = convert::id(request.id);
    if (Result<void> deleted = deps_.hosts.delete_profile(profile); !deleted) return std::unexpected(std::move(deleted.error()));
    // The profile's join password goes with it.
    if (Result<secrets::SecretTarget> target = secrets::SecretTarget::parse(
            secrets::SecretKind::HostJoinPassword, secrets::SecretScope::host_profile(profile).text()))
        deps_.secrets.clear(*target, [](Result<void> cleared) { log_failure("clearing a deleted profile's password", cleared); });
    return api::HostProfilesDeleteResponse{};
}

Result<api::HostShareLinkResponse> ApiRouter::share_link(const api::CallContext&, const api::HostShareLinkRequest& request) {
    Result<publish::ShareLink> link = deps_.hosts.share_link(convert::id(request.session));
    if (!link) return std::unexpected(std::move(link.error()));
    return api::HostShareLinkResponse{link->url(), convert::id(link->server)};
}

Result<api::HostCommandResponse> ApiRouter::command(const api::CallContext&, const api::HostCommandRequest& request) {
    Result<host::HostCommand> command = host_command(request.command);
    if (!command) return std::unexpected(std::move(command.error()));
    const SessionId session = convert::id(request.session);
    if (Result<void> sent = deps_.hosts.command(session, std::move(*command),
                                                [session](gameserver::CommandResult result) {
                                                    if (result.status == gameserver::CommandStatus::Ok) return;
                                                    REBOOT_LOG_WARN(Host, "an operator command to {} failed: {}",
                                                                    format_uuid(session.value),
                                                                    result.error ? result.error->id : "unsupported");
                                                });
        !sent)
        return std::unexpected(std::move(sent.error()));
    return api::HostCommandResponse{};
}

Result<OpHandle> ApiRouter::start(const api::CallContext&, const api::HostStartRequest& request, DisconnectPolicy disconnect) {
    if (Result<void> admitted = deps_.updates.admit_new_session(); !admitted) return std::unexpected(admitted.error());
    host::HostStartRequest start;
    start.profile = convert::id(request.profile);
    if (request.overrides.listing)
        start.overrides.listing =
            *request.overrides.listing == api::Listing::Listed ? storage::HostListing::Listed : storage::HostListing::Unlisted;
    if (request.overrides.port) {
        if (*request.overrides.port == 0 || *request.overrides.port > 0xFFFF)
            return std::unexpected(convert::invalid_request("overrides.port"));
        start.overrides.port = Port{static_cast<u16>(*request.overrides.port)};
    }
    if (request.overrides.build) start.overrides.build = convert::id(*request.overrides.build);
    start.disconnect = disconnect;
    if (request.linked_to) start.linked_to = convert::id(*request.linked_to);
    return deps_.hosts.start(std::move(start));
}

Result<api::HostStatusResponse> ApiRouter::status(const api::CallContext&, const api::HostStatusRequest& request) {
    Result<host::HostSnapshot> snapshot = deps_.hosts.status(convert::id(request.session));
    if (!snapshot) return std::unexpected(std::move(snapshot.error()));
    return api::HostStatusResponse{convert::host_status(*snapshot)};
}

Result<api::HostCancelMatchEndResponse> ApiRouter::cancel_match_end(const api::CallContext&,
                                                                    const api::HostCancelMatchEndRequest& request) {
    Result<bool> cancelled = deps_.hosts.cancel_match_end(convert::id(request.session));
    if (!cancelled) return std::unexpected(std::move(cancelled.error()));
    return api::HostCancelMatchEndResponse{*cancelled};
}

Result<OpHandle> ApiRouter::start_identity_export(const api::CallContext&, const api::HostIdentityExportRequest& request,
                                                  DisconnectPolicy disconnect) {
    Result<NativePath> destination = absolute_path(request.destination, "destination");
    if (!destination) return std::unexpected(std::move(destination.error()));
    return deps_.host_identities.start_export(convert::id(request.profile), std::move(*destination), disconnect);
}

Result<OpHandle> ApiRouter::start_identity_import(const api::CallContext&, const api::HostIdentityImportRequest& request,
                                                  DisconnectPolicy disconnect) {
    Result<NativePath> source = absolute_path(request.source, "source");
    if (!source) return std::unexpected(std::move(source.error()));
    const HostProfileId profile = convert::id(request.profile);
    // The identity store only sees a publication's hold; any live session keeps its identity.
    for (const sessions::SessionInfo& session : deps_.sessions.list()) {
        if (session.kind != sessions::SessionKind::Host || session.profile != profile || !sessions::is_live(session.phase))
            continue;
        host::HostError busy{.code = host::HostErrorCode::ProfileBusy, .profile = profile};
        for (const host::HostProfile& known : deps_.hosts.profiles())
            if (known.id == profile) busy.name = known.name;
        return std::unexpected(host::to_diagnostic(busy));
    }
    return deps_.host_identities.start_import(profile, std::move(*source), disconnect);
}

// Identity

namespace {

[[nodiscard]] contracts::backend::AccountRole account_role(api::GameRole role) {
    return role == api::GameRole::Host ? contracts::backend::AccountRole::Host : contracts::backend::AccountRole::Client;
}

}  // namespace

Result<api::IdentityGetResponse> ApiRouter::get(const api::CallContext&, const api::IdentityGetRequest&) {
    const identity::IdentitySnapshot& snapshot = deps_.identity.get();
    return api::IdentityGetResponse{{convert::identity_profile(snapshot.client), convert::identity_profile(snapshot.host)}};
}

Result<api::IdentitySetDisplayNameResponse> ApiRouter::set_display_name(const api::CallContext&,
                                                                        const api::IdentitySetDisplayNameRequest& request) {
    Result<identity::AccountRecord> record = deps_.identity.set_display_name(account_role(request.role), request.display_name);
    if (!record) return std::unexpected(std::move(record.error()));
    return api::IdentitySetDisplayNameResponse{convert::identity_profile(*record)};
}

Result<api::IdentityResetResponse> ApiRouter::reset(const api::CallContext&, const api::IdentityResetRequest& request) {
    Result<identity::AccountRecord> record = deps_.identity.reset(account_role(request.role));
    if (!record) return std::unexpected(std::move(record.error()));
    return api::IdentityResetResponse{convert::identity_profile(*record)};
}

// Install

Result<OpHandle> ApiRouter::start_suggest_destination(const api::CallContext&, const api::InstallSuggestDestinationRequest& request,
                                                      DisconnectPolicy disconnect) {
    return deps_.installer.start_suggest_destination(request.entry_id, disconnect);
}

Result<OpHandle> ApiRouter::start_install(const api::CallContext&, const api::InstallInstallRequest& request,
                                          DisconnectPolicy disconnect) {
    Result<NativePath> destination = absolute_path(request.destination, "destination");
    if (!destination) return std::unexpected(std::move(destination.error()));
    builds::InstallRequest install;
    install.entry = request.entry_id;
    install.destination = std::move(*destination);
    install.name = request.name;
    return deps_.installer.start_install(std::move(install), disconnect);
}

Result<OpHandle> ApiRouter::start_discard_staging(const api::CallContext&, const api::InstallDiscardStagingRequest& request,
                                                  DisconnectPolicy disconnect) {
    Result<NativePath> destination = absolute_path(request.destination, "destination");
    if (!destination) return std::unexpected(std::move(destination.error()));
    return deps_.installer.start_discard_staging(*destination, disconnect);
}

Result<OpHandle> ApiRouter::start_delete_unregistered(const api::CallContext&,
                                                      const api::InstallDeleteUnregisteredRequest& request,
                                                      DisconnectPolicy disconnect) {
    Result<NativePath> folder = absolute_path(request.folder, "folder");
    if (!folder) return std::unexpected(std::move(folder.error()));
    return deps_.installer.start_delete_unregistered(*folder, disconnect);
}

// Integration

Result<api::IntegrationStatusResponse> ApiRouter::status(const api::CallContext&, const api::IntegrationStatusRequest&) {
    // Each call also reads the entries again, since the user can change them in the OS at any time.
    if (!state_->integration_reading) {
        state_->integration_reading = true;
        const CancelToken alive = state_->alive.token();
        deps_.integration.status(alive, [this, alive](std::vector<integration::EntryStatus> entries) {
            if (alive.cancelled()) return;
            state_->integration = std::move(entries);
            state_->integration_reading = false;
        });
    }
    if (!state_->integration) return std::unexpected(not_ready("integration status"));
    api::IntegrationStatusResponse out;
    for (const integration::EntryStatus& entry : *state_->integration) out.items.push_back(convert::integration_entry(entry));
    return out;
}

Result<api::IntegrationPrerequisitesResponse> ApiRouter::prerequisites(const api::CallContext&,
                                                                       const api::IntegrationPrerequisitesRequest&) {
    if (!state_->prerequisites_reading) {
        state_->prerequisites_reading = true;
        const CancelToken alive = state_->alive.token();
        deps_.prerequisites.check(alive, [this, alive](std::vector<integration::Prerequisite> checked) {
            if (alive.cancelled()) return;
            state_->prerequisites = std::move(checked);
            state_->prerequisites_reading = false;
        });
    }
    if (!state_->prerequisites || (!state_->security && state_->security_reading))
        return std::unexpected(not_ready("prerequisites"));
    api::IntegrationPrerequisitesResponse out;
    for (const integration::Prerequisite& prerequisite : *state_->prerequisites)
        out.prerequisites.push_back(convert::prerequisite(prerequisite));
    if (state_->security)
        for (const std::string& name : state_->security->names) out.security_products.push_back(api::SecurityProduct{name, true});
    return out;
}

Result<api::IntegrationShellOpenUrlResponse> ApiRouter::shell_open_url(const api::CallContext& context,
                                                                       const api::IntegrationShellOpenUrlRequest& request) {
    if (!integration::is_openable_url(request.url)) return std::unexpected(convert::invalid_request("url"));
    deps_.shell.open_url(context.caller, request.url, state_->alive.token(),
                         [](Result<void> opened) { log_failure("opening a link", opened); });
    return api::IntegrationShellOpenUrlResponse{};
}

Result<api::IntegrationShellOpenPathResponse> ApiRouter::shell_open_path(const api::CallContext& context,
                                                                         const api::IntegrationShellOpenPathRequest& request) {
    Result<NativePath> path = absolute_path(request.path, "path");
    if (!path) return std::unexpected(std::move(path.error()));
    deps_.shell.open_path(context.caller, std::move(*path), state_->alive.token(),
                          [](Result<void> opened) { log_failure("opening a folder", opened); });
    return api::IntegrationShellOpenPathResponse{};
}

Result<api::IntegrationShellRevealResponse> ApiRouter::shell_reveal(const api::CallContext& context,
                                                                    const api::IntegrationShellRevealRequest& request) {
    Result<NativePath> path = absolute_path(request.path, "path");
    if (!path) return std::unexpected(std::move(path.error()));
    deps_.shell.reveal(context.caller, std::move(*path), state_->alive.token(),
                       [](Result<void> revealed) { log_failure("revealing a file", revealed); });
    return api::IntegrationShellRevealResponse{};
}

Result<OpHandle> ApiRouter::start_apply(const api::CallContext&, const api::IntegrationApplyRequest& request,
                                        DisconnectPolicy disconnect) {
    Result<std::vector<ports::IntegrationKind>> kinds = integration_kinds(request.items);
    if (!kinds) return std::unexpected(std::move(kinds.error()));
    return deps_.integration.start_apply(std::move(*kinds), disconnect);
}

Result<OpHandle> ApiRouter::start_remove(const api::CallContext&, const api::IntegrationRemoveRequest& request,
                                         DisconnectPolicy disconnect) {
    Result<std::vector<ports::IntegrationKind>> kinds = integration_kinds(request.items);
    if (!kinds) return std::unexpected(std::move(kinds.error()));
    return deps_.integration.start_remove(std::move(*kinds), disconnect);
}

Result<OpHandle> ApiRouter::start_remediate(const api::CallContext&, const api::IntegrationRemediateRequest& request,
                                            DisconnectPolicy disconnect) {
    const std::optional<integration::PrerequisiteId> id = integration::parse_prerequisite_id(request.prerequisite);
    if (!id)
        return std::unexpected(integration::to_diagnostic(integration::PrerequisiteError{
            .code = integration::PrerequisiteErrorCode::UnknownId, .text = request.prerequisite}));
    return deps_.prerequisites.start_remediate(*id, disconnect);
}

Result<OpHandle> ApiRouter::start_purge(const api::CallContext&, const api::IntegrationPurgeRequest& request,
                                        DisconnectPolicy disconnect) {
    if (request.scope == api::PurgeScope::Integration)
        return deps_.integration.start_remove(
            std::vector<ports::IntegrationKind>(integration::kAllIntegrationKinds.begin(), integration::kAllIntegrationKinds.end()),
            disconnect);
    integration::PurgeScope scope{};
    switch (request.scope) {
        case api::PurgeScope::Caches: scope = integration::PurgeScope::Cache; break;
        case api::PurgeScope::AllData: scope = integration::PurgeScope::All; break;
        default: return std::unexpected(convert::invalid_request("scope"));
    }
    if (request.running_policy != api::RunningPolicy::StopSessions) return deps_.purge.start_purge(scope, disconnect);

    // Sessions and the backend hold files in the scope, so they end before the purge starts.
    auto [handle, outer] = deps_.ops.create<std::any>(OpKind::Generic, disconnect, std::nullopt, RunnerMultiplier::Native,
                                                      std::chrono::minutes{10});
    const OpId outer_id = handle.id();
    const CancelToken alive = state_->alive.token();
    sessions::StopRequest stop;
    stop.reason = sessions::StopReason::User;
    deps_.sessions.stop_all(std::nullopt, std::move(stop), [this, &outer, outer_id, scope, disconnect, alive] {
        if (alive.cancelled()) {
            outer.complete(Cancelled{CancelReason::Shutdown});
            return;
        }
        deps_.backend.stop_for(backend::BackendStopCause::Reset, [this, &outer, outer_id, scope, disconnect, alive](Result<void>) {
            if (alive.cancelled()) {
                outer.complete(Cancelled{CancelReason::Shutdown});
                return;
            }
            if (outer.token().cancelled()) {
                outer.complete(Cancelled{*outer.token().reason()});
                return;
            }
            Result<OpHandle> inner = deps_.purge.start_purge(scope, disconnect);
            if (!inner) {
                outer.complete(Failed{std::move(inner.error())});
                return;
            }
            const OpId inner_id = inner->id();
            State::Relay relay;
            relay.inner_events = deps_.events.subscribe(EventFilter{{EventKind::OpCompleted}, std::nullopt, inner_id}, 4096);
            relay.cancel = outer.token().on_cancel([&ops = deps_.ops, inner_id](CancelReason reason) {
                static_cast<void>(ops.cancel(inner_id, reason));
            });
            // The subscription is dropped from a posted task, never from inside its own notify.
            const auto finish = [this, &outer, outer_id, alive](const ErasedOutcome& outcome) {
                std::visit(
                    [&outer]<class A>(const A& alternative) {
                        if constexpr (std::is_same_v<A, Completed<std::any>>) outer.complete(Completed<std::any>{alternative.value});
                        else outer.complete(alternative);
                    },
                    outcome);
                deps_.strand.post([this, outer_id, alive] {
                    if (alive.cancelled()) return;
                    if (const auto relay_it = state_->relays.find(outer_id); relay_it != state_->relays.end()) {
                        relay_it->second.inner_events->set_notify({});
                        state_->relays.erase(relay_it);
                    }
                });
            };
            relay.inner_events->set_notify([this, outer_id, finish] {
                const auto relay_it = state_->relays.find(outer_id);
                if (relay_it == state_->relays.end()) return;
                std::vector<EventEnvelope> events;
                relay_it->second.inner_events->drain(events, 16);
                for (const EventEnvelope& event : events)
                    if (const auto* completed = std::any_cast<OpCompletedEvent>(&event.payload)) finish(completed->outcome);
            });
            state_->relays.insert_or_assign(outer_id, std::move(relay));
            if (std::optional<ErasedOutcome> done = deps_.ops.outcome(inner_id)) finish(*done);
        });
    });
    return handle;
}

// Join

Result<api::JoinParseAddressResponse> ApiRouter::parse_address(const api::CallContext&, const api::JoinParseAddressRequest& request) {
    Result<HostPort> address = browser::parse_game_server_address(request.text);
    if (!address) return std::unexpected(std::move(address.error()));
    return api::JoinParseAddressResponse{address->host, address->port ? address->port->value : 0u};
}

Result<api::JoinTargetResponse> ApiRouter::target(const api::CallContext&, const api::JoinTargetRequest&) {
    api::JoinTargetResponse out;
    if (const std::optional<browser::JoinTarget>& current = deps_.addresses.current()) out.target = convert::join_target(*current);
    return out;
}

Result<api::JoinSetCustomTargetResponse> ApiRouter::set_custom_target(const api::CallContext&,
                                                                      const api::JoinSetCustomTargetRequest& request) {
    Result<OpHandle> started = deps_.addresses.start_set_custom(request.address, DisconnectPolicy::Detached);
    if (!started) return std::unexpected(std::move(started.error()));
    // The target is stored once the name resolves; JoinTargetChanged says when.
    api::JoinTarget target;
    target.address = std::string(trim(request.address));
    return api::JoinSetCustomTargetResponse{std::move(target)};
}

Result<api::JoinClearTargetResponse> ApiRouter::clear_target(const api::CallContext&, const api::JoinClearTargetRequest&) {
    deps_.addresses.clear();
    return api::JoinClearTargetResponse{};
}

Result<OpHandle> ApiRouter::start_resolve_link(const api::CallContext&, const api::JoinResolveLinkRequest& request,
                                               DisconnectPolicy disconnect) {
    return deps_.deep_links.start_resolve(request.url, disconnect);
}

Result<OpHandle> ApiRouter::start_grant(const api::CallContext&, const api::JoinGrantRequest& request, DisconnectPolicy disconnect) {
    browser::JoinRequest join;
    join.server = convert::id(request.server);
    return deps_.join.start_join(std::move(join), disconnect);
}

// Library

Result<api::LibraryListResponse> ApiRouter::list(const api::CallContext&, const api::LibraryListRequest&) {
    api::LibraryListResponse out;
    for (const builds::InstalledBuild& build : deps_.library.list()) out.builds.push_back(convert::build(build));
    if (const std::optional<BuildId> client = deps_.library.selected(support::SupportRole::Play)) out.selected_client = convert::id(*client);
    if (const std::optional<BuildId> host = deps_.library.selected(support::SupportRole::Host)) out.selected_host = convert::id(*host);
    return out;
}

Result<api::LibraryGetResponse> ApiRouter::get(const api::CallContext&, const api::LibraryGetRequest& request) {
    Result<builds::InstalledBuild> build = deps_.library.get(convert::id(request.id));
    if (!build) return std::unexpected(std::move(build.error()));
    return api::LibraryGetResponse{convert::build(*build)};
}

Result<api::LibrarySelectResponse> ApiRouter::select(const api::CallContext&, const api::LibrarySelectRequest& request) {
    std::optional<BuildId> id;
    if (request.id) id = convert::id(*request.id);
    if (Result<void> selected = deps_.library.select(convert::role(request.role), id); !selected)
        return std::unexpected(std::move(selected.error()));
    return api::LibrarySelectResponse{};
}

Result<api::LibraryUpdateResponse> ApiRouter::update(const api::CallContext&, const api::LibraryUpdateRequest& request) {
    builds::BuildPatch patch;
    patch.name = request.name;
    if (request.version) {
        Result<GameVersion> version = convert::game_version(*request.version, "version");
        if (!version) return std::unexpected(std::move(version.error()));
        patch.version = *version;
    }
    if (request.changelist) patch.cl = Changelist{*request.changelist};
    Result<builds::InstalledBuild> updated = deps_.library.update(convert::id(request.id), patch);
    if (!updated) return std::unexpected(std::move(updated.error()));
    return api::LibraryUpdateResponse{convert::build(*updated)};
}

Result<OpHandle> ApiRouter::start_relocate(const api::CallContext&, const api::LibraryRelocateRequest& request,
                                           DisconnectPolicy disconnect) {
    Result<NativePath> root = absolute_path(request.root, "root");
    if (!root) return std::unexpected(std::move(root.error()));
    return deps_.library.start_relocate(convert::id(request.id), std::move(*root), build_policy(request.running_policy),
                                        disconnect);
}

Result<OpHandle> ApiRouter::start_import(const api::CallContext&, const api::LibraryImportRequest& request,
                                         DisconnectPolicy disconnect) {
    Result<NativePath> path = absolute_path(request.path, "path");
    if (!path) return std::unexpected(std::move(path.error()));
    builds::ImportRequest import_request;
    import_request.name = request.name.empty() ? deps_.build_import.suggest_name(*path) : request.name;
    import_request.path = std::move(*path);
    if (request.version) {
        Result<GameVersion> version = convert::game_version(*request.version, "version");
        if (!version) return std::unexpected(std::move(version.error()));
        builds::UserVersion stated{*version, std::nullopt};
        if (request.changelist) stated.cl = Changelist{*request.changelist};
        import_request.version = stated;
    }
    return deps_.build_import.start_import(std::move(import_request), disconnect);
}

Result<OpHandle> ApiRouter::start_remove(const api::CallContext&, const api::LibraryRemoveRequest& request,
                                         DisconnectPolicy disconnect) {
    builds::RemoveFiles files = builds::RemoveFiles::Keep;
    if (request.files == api::RemoveFiles::Trash) files = builds::RemoveFiles::Trash;
    else if (request.files == api::RemoveFiles::Delete) files = builds::RemoveFiles::Delete;
    return deps_.library.start_remove(convert::id(request.id), files, build_policy(request.running_policy), disconnect);
}

// Logs

Result<api::LogsReadResponse> ApiRouter::read(const api::CallContext&, const api::LogsReadRequest& request) {
    logging::LogFilter filter;
    filter.min_level = static_cast<LogLevel>(std::to_underlying(request.filter.min_level));
    for (const api::LogCategory category : request.filter.categories)
        filter.categories.push_back(static_cast<LogCategory>(std::to_underlying(category)));
    if (request.filter.session) filter.session = convert::id(*request.filter.session);
    const u32 limit = request.limit == 0 ? kDefaultLogPage : std::min(request.limit, kMaxLogPage);
    const logging::LogPage page = deps_.log_ring.read(logging::LogCursor{request.cursor}, filter, limit);
    api::LogsReadResponse out;
    for (const logging::LogEntry& entry : page.entries) out.entries.push_back(convert::log_entry(entry));
    out.next_cursor = page.next.after_seq;
    out.skipped = page.missed;
    return out;
}

Result<OpHandle> ApiRouter::start_export(const api::CallContext&, const api::LogsExportRequest& request,
                                         DisconnectPolicy disconnect) {
    Result<NativePath> destination = absolute_path(request.destination, "destination");
    if (!destination) return std::unexpected(std::move(destination.error()));
    std::string summary = std::format("engine {} ({})\nos {} {} {}\norigin {}\nstorage {}\n", deps_.info.build,
                                      deps_.info.app_version.to_string(), deps_.info.os.name, deps_.info.os.version,
                                      deps_.info.os.arch, origin_name(deps_.lifecycle.origin()),
                                      static_cast<int>(deps_.info.storage_mode));
    for (const sessions::SessionInfo& session : deps_.sessions.list())
        summary += std::format("session {} {} {} {}\n", format_uuid(session.id.value),
                               session.kind == sessions::SessionKind::Play ? "play" : "host",
                               sessions::session_phase_name(session.phase), session.version.canonical());
    return deps_.log_exporter.start_export(logging::LogExportRequest{std::move(*destination), std::move(summary)}, disconnect);
}

// Play

namespace {

[[nodiscard]] Result<play::PlayRequest> play_request(const api::CallContext& context, const api::PlayRequest& request) {
    play::PlayRequest out;
    if (request.build) out.build = convert::id(*request.build);
    if (request.auto_server && request.target)
        return make_diag(ErrorDomain::Engine, msg::kConflictingPlayTarget).kind(ErrorKind::InvalidInput).fail();
    if (request.auto_server) {
        out.target = play::AutoServerTarget{};
    } else if (request.target) {
        if (request.target->server) {
            out.target = play::BrowserServerTarget{convert::id(*request.target->server)};
        } else if (request.target->address) {
            Result<HostPort> address = browser::parse_game_server_address(*request.target->address);
            if (!address) return std::unexpected(std::move(address.error()));
            out.target = browser::AddressTarget{*request.target->address, std::move(*address)};
        }
    }
    if (request.backend) {
        Result<backend::BackendTarget> backend = convert::backend_target(*request.backend);
        if (!backend) return std::unexpected(std::move(backend.error()));
        out.backend = std::move(*backend);
    }
    out.custom_args = request.custom_args;
    out.display = context.caller;
    if (!request.environment.empty()) {
        out.display.display_env.clear();
        for (const api::EnvVar& variable : request.environment)
            out.display.display_env.push_back(contracts::ipc::EnvVar{variable.name, variable.value});
    }
    return out;
}

}  // namespace

Result<api::PlayPlanResponse> ApiRouter::plan(const api::CallContext& context, const api::PlayPlanRequest& request) {
    Result<play::PlayRequest> play_req = play_request(context, request.request);
    if (!play_req) return std::unexpected(std::move(play_req.error()));
    Result<play::PlayPlan> planned = deps_.play.plan(*play_req);
    if (!planned) return std::unexpected(std::move(planned.error()));
    api::PlayPlanResponse out;
    out.tier = convert::tier(planned->support.tier);
    out.net_mode = planned->net_mode == injection::NetMode::LegacyFixed ? api::NetMode::LegacyFixed : api::NetMode::PerSession;
    out.runner = convert::runner(planned->runner);
    out.blockers = convert::diagnostics(planned->blockers);
    out.warnings = convert::diagnostics(planned->warnings);
    for (const play::RequiredDecision& decision : planned->decisions)
        out.questions.push_back(static_cast<api::UserRequestKind>(std::to_underlying(decision.kind)));
    return out;
}

Result<OpHandle> ApiRouter::start(const api::CallContext& context, const api::PlayStartRequest& request, DisconnectPolicy disconnect) {
    if (Result<void> admitted = deps_.updates.admit_new_session(); !admitted) return std::unexpected(admitted.error());
    Result<play::PlayRequest> play_req = play_request(context, request.request);
    if (!play_req) return std::unexpected(std::move(play_req.error()));
    return deps_.play.start(std::move(*play_req), disconnect);
}

// Requests

Result<api::RequestsPendingResponse> ApiRouter::pending(const api::CallContext&, const api::RequestsPendingRequest&) {
    api::RequestsPendingResponse out;
    for (const UserRequest& request : deps_.requests.pending()) out.requests.push_back(requests::user_action(deps_, request));
    return out;
}

Result<api::RequestsRespondResponse> ApiRouter::respond(const api::CallContext&, const api::RequestsRespondRequest& request) {
    const RequestId id{request.request_id};
    std::any answer;
    for (const UserRequest& pending : deps_.requests.pending()) {
        if (pending.id != id) continue;
        Result<std::any> converted = requests::answer_for(pending, request.answer);
        if (!converted) return std::unexpected(std::move(converted.error()));
        answer = std::move(*converted);
        break;
    }
    // An id that is no longer pending is answered by the registry with its own error.
    if (Result<void> responded = deps_.requests.respond(id, std::move(answer)); !responded)
        return std::unexpected(std::move(responded.error()));
    return api::RequestsRespondResponse{};
}

// Secrets

Result<api::SecretsStateResponse> ApiRouter::state(const api::CallContext&, const api::SecretsStateRequest& request) {
    Result<secrets::SecretTarget> target = convert::secret_target(request.target);
    if (!target) return std::unexpected(std::move(target.error()));
    Result<secrets::SecretState> state = deps_.secrets.state(*target);
    if (!state) return std::unexpected(std::move(state.error()));
    return api::SecretsStateResponse{state->present(), convert::secret_store(state->location)};
}

Result<api::SecretsClearResponse> ApiRouter::clear(const api::CallContext&, const api::SecretsClearRequest& request) {
    Result<secrets::SecretTarget> target = convert::secret_target(request.target);
    if (!target) return std::unexpected(std::move(target.error()));
    deps_.secrets.clear(*target, [](Result<void> cleared) { log_failure("erasing a stored secret", cleared); });
    if (target->kind == secrets::SecretKind::HostJoinPassword && request.target.host_profile)
        if (Result<void> refreshed = deps_.hosts.refresh_join_password(convert::id(*request.target.host_profile)); !refreshed)
            REBOOT_LOG_WARN(Host, "live sessions keep their join password: {}", refreshed.error().id);
    return api::SecretsClearResponse{};
}

// Sessions

Result<api::SessionsListResponse> ApiRouter::list(const api::CallContext&, const api::SessionsListRequest&) {
    api::SessionsListResponse out;
    for (const sessions::SessionInfo& session : deps_.sessions.list()) out.sessions.push_back(convert::session_summary(session));
    return out;
}

Result<api::SessionsGetResponse> ApiRouter::get(const api::CallContext&, const api::SessionsGetRequest& request) {
    Result<sessions::SessionInfo> info = deps_.sessions.get(convert::id(request.id));
    if (!info) return std::unexpected(std::move(info.error()));
    api::SessionsGetResponse out;
    out.summary = convert::session_summary(*info);
    api::SessionDetail& detail = out.detail;
    detail.lease = info->lease.held_by_engine() ? api::LeaseKind::Engine : api::LeaseKind::Client;
    if (info->pinned.build) detail.build = convert::id(*info->pinned.build);
    detail.version = convert::game_version(info->version);
    detail.runner = convert::runner(info->runner);
    if (info->kind == sessions::SessionKind::Play)
        if (const std::optional<play::PlaySessionState> play_state = deps_.play.state(info->id))
            detail.net_mode =
                play_state->net_mode == injection::NetMode::LegacyFixed ? api::NetMode::LegacyFixed : api::NetMode::PerSession;
    detail.tier = api::SupportTier::Unknown;
    if (info->profile) detail.profile = convert::id(*info->profile);
    for (const sessions::SpawnedProcess& process : info->processes)
        detail.processes.push_back(api::SessionProcess{convert::process_role(process.role), process.pid});
    detail.degraded = convert::diagnostics(info->degraded);
    return out;
}

Result<api::SessionsSetLeaseResponse> ApiRouter::set_lease(const api::CallContext& context, const api::SessionsSetLeaseRequest& request) {
    const sessions::Lease lease =
        request.lease == api::LeaseKind::Client ? sessions::Lease::client_of(context.connection) : sessions::Lease::engine();
    if (Result<void> set = deps_.sessions.set_lease(convert::id(request.id), lease); !set) return std::unexpected(std::move(set.error()));
    return api::SessionsSetLeaseResponse{};
}

Result<OpHandle> ApiRouter::start_stop(const api::CallContext&, const api::SessionsStopRequest& request, DisconnectPolicy disconnect) {
    const SessionId session = convert::id(request.id);
    if (Result<sessions::SessionInfo> known = deps_.sessions.get(session); !known) return std::unexpected(std::move(known.error()));
    sessions::StopRequest stop;
    stop.reason = sessions::StopReason::User;
    if (request.grace_ms != 0) stop.grace = std::chrono::milliseconds{request.grace_ms};
    const std::chrono::milliseconds bound = stop.grace + sessions::kStopKillMargin + std::chrono::seconds{5};
    auto [handle, op] = deps_.ops.create<void>(OpKind::GracefulStop, disconnect, session, RunnerMultiplier::Native, bound);
    Operation<void>* stopping = &op;
    if (Result<void> stopped = deps_.sessions.stop(session, std::move(stop), [stopping] { stopping->complete(Completed<void>{}); });
        !stopped)
        op.complete(Failed{std::move(stopped.error())});
    return handle;
}

// Settings

Result<api::SettingsSnapshotResponse> ApiRouter::snapshot(const api::CallContext&, const api::SettingsSnapshotRequest&) {
    const storage::SettingsSnapshot snapshot = deps_.settings.snapshot();
    const storage::SettingsValues defaults;
    api::SettingsSnapshotResponse out;
    out.revision = snapshot.revision;
    for (const storage::AnyKey* key : deps_.settings_registry.all()) {
        api::Setting setting;
        setting.key = std::string(key->spec().id);
        setting.value = setting_value(key->spec().kind, key->encode(snapshot.values));
        setting.is_default = !key->differs(snapshot.values, defaults);
        out.settings.push_back(std::move(setting));
    }
    return out;
}

Result<api::SettingsPatchResponse> ApiRouter::patch(const api::CallContext&, const api::SettingsPatchRequest& request) {
    const storage::SettingsSnapshot snapshot = deps_.settings.snapshot();
    storage::SettingsValues next = snapshot.values;
    for (std::size_t i = 0; i < request.changes.size(); ++i) {
        const api::Setting& change = request.changes[i];
        const storage::AnyKey* key = deps_.settings_registry.find(change.key);
        if (key == nullptr) {
            Result<json::value> unknown = deps_.settings.get(change.key);
            return std::unexpected(unknown ? convert::invalid_request("changes.key") : std::move(unknown.error()));
        }
        Result<json::value> value = setting_json(key->spec().kind, change.value, "changes.value");
        if (!value) return std::unexpected(std::move(value.error()));
        if (Result<void> decoded = key->decode_into(next, *value); !decoded) return std::unexpected(std::move(decoded.error()));
    }
    for (const std::string& id : request.reset_keys) {
        const storage::AnyKey* key = deps_.settings_registry.find(id);
        if (key == nullptr) {
            Result<json::value> unknown = deps_.settings.get(id);
            return std::unexpected(unknown ? convert::invalid_request("reset_keys") : std::move(unknown.error()));
        }
        key->reset(next);
    }
    storage::SettingsPatch patch = diff(snapshot.values, next);
    patch.expected_revision = request.expected_revision;
    Result<u64> revision = deps_.settings.patch(patch);
    if (!revision) return std::unexpected(std::move(revision.error()));
    return api::SettingsPatchResponse{*revision};
}

Result<api::SettingsResetResponse> ApiRouter::reset(const api::CallContext&, const api::SettingsResetRequest& request) {
    const std::optional<storage::ResetGroup> group = reset_group(request.group);
    if (!group) return std::unexpected(convert::invalid_request("group"));
    Result<storage::ResetReport> report = deps_.reset.reset(*group);
    if (report) return api::SettingsResetResponse{report->revision, report->keys};
    if (request.running_policy != api::RunningPolicy::StopSessions || report.error().id != "storage.reset_blocked")
        return std::unexpected(std::move(report.error()));
    Result<OpHandle> deferred = deps_.reset.start_reset_after_stop(*group, DisconnectPolicy::Detached);
    if (!deferred) return std::unexpected(std::move(deferred.error()));
    return api::SettingsResetResponse{0, {}};
}

Result<api::SettingsSearchResponse> ApiRouter::search(const api::CallContext&, const api::SettingsSearchRequest& request) {
    api::SettingsSearchResponse out;
    for (const ux::SettingMatch& match : deps_.settings_search.search(request.text, *state_->catalog, kSearchLimit))
        out.hits.push_back(api::SettingSearchHit{match.key, match.score});
    return out;
}

Result<api::SettingsFrontendStateGetResponse> ApiRouter::frontend_state_get(const api::CallContext&,
                                                                            const api::SettingsFrontendStateGetRequest& request) {
    Result<storage::ShellName> shell = storage::ShellName::parse(request.shell);
    if (!shell) return std::unexpected(std::move(shell.error()));
    if (const auto cached = state_->frontend.find(shell->value); cached != state_->frontend.end())
        return api::SettingsFrontendStateGetResponse{cached->second};
    const CancelToken alive = state_->alive.token();
    deps_.frontend_state.get(*shell, alive, [this, alive, key = shell->value](Result<std::vector<u8>> blob) {
        if (alive.cancelled() || !blob || state_->frontend.contains(key)) return;
        state_->frontend.insert_or_assign(key, std::move(*blob));
    });
    return std::unexpected(not_ready("the frontend state"));
}

Result<api::SettingsFrontendStatePutResponse> ApiRouter::frontend_state_put(const api::CallContext&,
                                                                            const api::SettingsFrontendStatePutRequest& request) {
    Result<storage::ShellName> shell = storage::ShellName::parse(request.shell);
    if (!shell) return std::unexpected(std::move(shell.error()));
    // The store checks the blob the same way, but answers later; a call has to answer now.
    if (request.state.size() > storage::kFrontendStateMaxBytes) return std::unexpected(convert::invalid_request("state"));
    const std::string_view text(reinterpret_cast<const char*>(request.state.data()), request.state.size());
    boost::system::error_code error;
    static_cast<void>(json::parse(text, error));
    if (!is_valid_utf8(text) || error) return std::unexpected(convert::invalid_request("state"));
    state_->frontend.insert_or_assign(shell->value, request.state);
    deps_.frontend_state.put(*shell, request.state, CancelToken{},
                             [](Result<void> written) { log_failure("saving the frontend state", written); });
    return api::SettingsFrontendStatePutResponse{};
}

Result<api::SettingsLanguagesResponse> ApiRouter::languages(const api::CallContext&, const api::SettingsLanguagesRequest&) {
    api::SettingsLanguagesResponse out;
    for (const ux::LanguageInfo& language : ux::shipped_languages())
        out.languages.push_back(api::Language{language.tag.str(), language.shipped});
    out.selected = deps_.settings.snapshot().values.ui.language;
    if (Result<ux::LanguagePreference> preference = ux::LanguagePreference::parse(out.selected);
        preference && preference->tag() &&
        std::ranges::none_of(out.languages, [&](const api::Language& known) { return known.tag == preference->tag()->str(); }))
        out.languages.push_back(api::Language{preference->tag()->str(), false});
    out.resolved = ui_language(deps_.settings).str();
    return out;
}

Result<api::SettingsDescriptorsResponse> ApiRouter::descriptors(const api::CallContext&, const api::SettingsDescriptorsRequest&) {
    api::SettingsDescriptorsResponse out;
    for (const storage::AnyKey* key : deps_.settings_registry.all()) {
        const storage::KeySpec& spec = key->spec();
        api::SettingDescriptor descriptor;
        descriptor.key = std::string(spec.id);
        descriptor.label = std::string(spec.label.id);
        descriptor.kind = static_cast<api::SettingKind>(std::to_underlying(spec.kind));
        for (const std::string_view choice : spec.choices) descriptor.choices.emplace_back(choice);
        if (spec.reset_group) descriptor.reset_group = static_cast<api::SettingGroup>(std::to_underlying(*spec.reset_group));
        descriptor.sensitivity = static_cast<api::Sensitivity>(std::to_underlying(spec.sensitivity));
        out.descriptors.push_back(std::move(descriptor));
    }
    return out;
}

Result<api::SettingsConsoleKeysResponse> ApiRouter::console_keys(const api::CallContext&, const api::SettingsConsoleKeysRequest&) {
    api::SettingsConsoleKeysResponse out;
    for (const std::string_view name : storage::unreal_key_names()) {
        const storage::ConsoleKey key{std::string(name)};
        out.keys.push_back(api::ConsoleKey{key.name, std::string(backend::key_label(key))});
    }
    return out;
}

Result<api::SettingsConsoleKeyFromNativeResponse> ApiRouter::console_key_from_native(
    const api::CallContext&, const api::SettingsConsoleKeyFromNativeRequest& request) {
    if (request.code > std::numeric_limits<u16>::max()) return api::SettingsConsoleKeyFromNativeResponse{};
    const u16 code = static_cast<u16>(request.code);
    std::optional<backend::HidUsage> usage;
    switch (request.source) {
        case api::KeySource::HidUsage: usage = backend::HidUsage{code}; break;
        case api::KeySource::WindowsScancode: usage = backend::hid_from_windows_scancode(code, request.extended); break;
        case api::KeySource::MacosKeycode: usage = backend::hid_from_macos_keycode(code); break;
        case api::KeySource::Evdev: usage = backend::hid_from_evdev(code); break;
    }
    api::SettingsConsoleKeyFromNativeResponse out;
    if (!usage) return out;
    if (const std::optional<storage::ConsoleKey> key = backend::console_key_from_hid(*usage))
        out.key = api::ConsoleKey{key->name, std::string(backend::key_label(*key))};
    return out;
}

// Support

Result<api::SupportQueryResponse> ApiRouter::query(const api::CallContext&, const api::SupportQueryRequest& request) {
    support::SupportQuery query;
    query.role = convert::role(request.role);
    if (request.build) {
        Result<builds::InstalledBuild> build = deps_.library.get(convert::id(*request.build));
        if (!build) return std::unexpected(std::move(build.error()));
        if (build->version_confirmed()) query.version = build->version;
        query.cl = build->cl;
        query.imported = true;
    } else if (request.version) {
        Result<GameVersion> version = convert::game_version(request.version->version, "version");
        if (!version) return std::unexpected(std::move(version.error()));
        query.version = *version;
        if (request.version->changelist) query.cl = Changelist{*request.version->changelist};
    } else {
        return std::unexpected(convert::invalid_request("subject"));
    }
    if (request.runner) {
        query.runner = convert::runner(*request.runner);
    } else if (query.role == support::SupportRole::Play) {
        const std::vector<ports::RunnerKind> supported = deps_.runtimes.supported();
        query.runner = supported.empty() ? ports::RunnerKind::Native : supported.front();
    }
    const storage::SettingsValues values = deps_.settings.snapshot().values;
    if (query.role == support::SupportRole::Play) {
        query.custom_auth_dll = values.play.custom_auth_dll.has_value();
        query.embedded_backend = values.backend.target.kind == storage::BackendKind::Embedded;
    } else if (const gameserver::DescribedBinary* binary = deps_.game_server.current()) {
        if (Result<support::HostInputs> inputs = support::host_inputs_from(binary->sha256, binary->description))
            query.server = std::move(*inputs);
    }
    return convert::support_answer(query, deps_.support.evaluate(query));
}

// Updates

Result<api::UpdatesStatusResponse> ApiRouter::status(const api::CallContext&, const api::UpdatesStatusRequest&) {
    const updates::UpdateState state = deps_.updates.state();
    api::UpdatesStatusResponse out;
    out.current = convert::semver(state.installed);
    out.phase = convert::update_phase(state.phase);
    if (state.offer) out.available = convert::update_info(*state.offer);
    if (state.last_check) out.last_check_unix_ms = convert::unix_ms(*state.last_check);
    if (state.last_error) out.last_error = convert::diagnostic(*state.last_error);
    return out;
}

Result<OpHandle> ApiRouter::start_check(const api::CallContext&, const api::UpdatesCheckRequest&, DisconnectPolicy disconnect) {
    return deps_.updates.start_check(updates::CheckTrigger::User, disconnect);
}

Result<OpHandle> ApiRouter::start_apply(const api::CallContext&, const api::UpdatesApplyRequest& request, DisconnectPolicy) {
    return deps_.updates.start_apply(request.when == api::ApplyWhen::Now ? updates::ApplyWhen::Now : updates::ApplyWhen::WhenIdle);
}

}  // namespace rb::engine
