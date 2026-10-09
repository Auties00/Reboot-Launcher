#pragma once

#include <chrono>
#include <optional>
#include <string_view>
#include <vector>

#include "reboot/api/v1/backend.hpp"
#include "reboot/api/v1/browser.hpp"
#include "reboot/api/v1/catalog.hpp"
#include "reboot/api/v1/common.hpp"
#include "reboot/api/v1/components.hpp"
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
#include "reboot/backend/backend_account.hpp"
#include "reboot/backend/backend_event.hpp"
#include "reboot/backend/backend_state.hpp"
#include "reboot/backend/backend_target.hpp"
#include "reboot/browser/connection_state.hpp"
#include "reboot/browser/join_target.hpp"
#include "reboot/browser/list_state.hpp"
#include "reboot/browser/server_row.hpp"
#include "reboot/browser/view_spec.hpp"
#include "reboot/builds/destination_suggestion.hpp"
#include "reboot/builds/installed_build.hpp"
#include "reboot/catalog/catalog_entry.hpp"
#include "reboot/catalog/catalog_source.hpp"
#include "reboot/components/component_info.hpp"
#include "reboot/components/component_problem.hpp"
#include "reboot/engine/engine_lifecycle.hpp"
#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/events.hpp"
#include "reboot/foundation/native_path.hpp"
#include "reboot/foundation/operation.hpp"
#include "reboot/foundation/types.hpp"
#include "reboot/foundation/version.hpp"
#include "reboot/gameserver/game_server_event.hpp"
#include "reboot/host/host_event.hpp"
#include "reboot/host/host_profile.hpp"
#include "reboot/host/host_snapshot.hpp"
#include "reboot/identity/account_record.hpp"
#include "reboot/integration/entry_status.hpp"
#include "reboot/integration/prerequisite.hpp"
#include "reboot/logging/error_router.hpp"
#include "reboot/logging/log_ring.hpp"
#include "reboot/net/port_mapping.hpp"
#include "reboot/ports/runner.hpp"
#include "reboot/publish/publish_state.hpp"
#include "reboot/publish/reachability_changed.hpp"
#include "reboot/secrets/secret_state.hpp"
#include "reboot/secrets/secret_target.hpp"
#include "reboot/sessions/session_event.hpp"
#include "reboot/sessions/session_info.hpp"
#include "reboot/storage/backend_target.hpp"
#include "reboot/support/support_query.hpp"
#include "reboot/support/support_role.hpp"
#include "reboot/support/support_verdict.hpp"
#include "reboot/updates/update_offer.hpp"
#include "reboot/updates/update_state.hpp"
#include "reboot/ux/notice.hpp"
#include "reboot/ux/onboarding.hpp"
#include "reboot/ux/suggested_action.hpp"

// Domain values to reboot.api.v1 messages and back. Only ApiRouter uses these.
namespace rb::engine::convert {

// Values, ids and enums.
[[nodiscard]] api::Diagnostic diagnostic(const Diagnostic& diag);
[[nodiscard]] std::vector<api::Diagnostic> diagnostics(const std::vector<Diagnostic>& diags);
[[nodiscard]] api::Path path(const NativePath& native);
// engine.invalid_request naming `field` when the bytes are no path on this OS.
[[nodiscard]] Result<NativePath> path(const api::Path& wire, std::string_view field);
[[nodiscard]] api::SemVer semver(const SemVer& version);
[[nodiscard]] api::GameVersion game_version(const GameVersion& version);
// Strict, as GameVersion::parse; engine.invalid_request naming `field` otherwise.
[[nodiscard]] Result<GameVersion> game_version(const api::GameVersion& version, std::string_view field);
[[nodiscard]] u64 unix_ms(std::chrono::system_clock::time_point time);
[[nodiscard]] std::chrono::system_clock::time_point from_unix_ms(u64 ms);
[[nodiscard]] Diagnostic invalid_request(std::string_view field);

[[nodiscard]] inline api::SessionId id(const SessionId& id) { return api::SessionId{id.value}; }
[[nodiscard]] inline api::BuildId id(const BuildId& id) { return api::BuildId{id.value}; }
[[nodiscard]] inline api::HostProfileId id(const HostProfileId& id) { return api::HostProfileId{id.value}; }
[[nodiscard]] inline api::ServerId id(const ServerId& id) { return api::ServerId{id.value}; }
[[nodiscard]] inline SessionId id(const api::SessionId& id) { return SessionId{id.uuid}; }
[[nodiscard]] inline BuildId id(const api::BuildId& id) { return BuildId{id.uuid}; }
[[nodiscard]] inline HostProfileId id(const api::HostProfileId& id) { return HostProfileId{id.uuid}; }
[[nodiscard]] inline ServerId id(const api::ServerId& id) { return ServerId{id.uuid}; }

[[nodiscard]] api::RunnerKind runner(ports::RunnerKind kind);
[[nodiscard]] ports::RunnerKind runner(api::RunnerKind kind);
[[nodiscard]] api::GameRole role(support::SupportRole role);
[[nodiscard]] support::SupportRole role(api::GameRole role);
[[nodiscard]] api::SupportTier tier(support::SupportTier tier);
[[nodiscard]] api::CancelReason cancel_reason(CancelReason reason);

// The API event kind of a domain one; nullopt for ForegroundHint, which the API does not carry.
[[nodiscard]] std::optional<api::EventKind> event_kind(EventKind kind);
// nullopt for the client-side kinds (Resync, ConnectionLost, Reconnected) and unknown values.
[[nodiscard]] std::optional<EventKind> event_kind(api::EventKind kind);

// Engine.
[[nodiscard]] api::EnginePhase engine_phase(EnginePhase phase);
[[nodiscard]] api::EngineState engine_state(const EngineStateEvent& state);
[[nodiscard]] api::StorageModeChanged storage_mode_changed(const storage::StorageModeChanged& changed);
[[nodiscard]] api::Progress progress(const OpProgressEvent& progress, u32 method_id);

// Settings and guidance.
[[nodiscard]] api::NoticeKey notice_key(const ux::NoticeKey& key);
[[nodiscard]] api::Notice notice(const ux::Notice& notice);
[[nodiscard]] api::SuggestedAction suggested_action(const ux::SuggestedAction& action);
[[nodiscard]] api::OnboardingState onboarding(const ux::OnboardingView& view);
[[nodiscard]] api::MessageText message_text(MessageId id, const std::vector<std::pair<std::string, Arg>>& args);
// A background failure as the notice UIs list; dismissing its key acknowledges it.
[[nodiscard]] api::Notice background_notice(const logging::BackgroundFailure& failure);
[[nodiscard]] api::NoticeKey background_notice_key(logging::BackgroundFailureId id, const std::optional<SessionId>& session);
// The failure a background_notice_key names; nullopt for any other key.
[[nodiscard]] std::optional<logging::BackgroundFailureId> background_failure_of(const api::NoticeKey& key);

// Library, catalog, install, components.
[[nodiscard]] api::Build build(const builds::InstalledBuild& build);
[[nodiscard]] api::CatalogEntry catalog_entry(const catalog::CatalogEntry& entry, bool installed);
[[nodiscard]] api::CatalogSource catalog_source(catalog::CatalogOrigin origin);
[[nodiscard]] api::InstallSuggestDestinationResponse destination(const builds::DestinationSuggestion& suggestion);
// Every version of one component id; the selected one decides the state.
[[nodiscard]] api::Component component(const std::vector<components::ComponentInfo>& versions,
                                       const std::optional<components::ComponentProblem>& problem);
[[nodiscard]] std::vector<api::Component> components(const std::vector<components::ComponentInfo>& all);
[[nodiscard]] api::ComponentProblem component_problem(const components::ComponentProblem& problem);

// Identity, secrets, backend.
[[nodiscard]] api::IdentityProfile identity_profile(const identity::AccountRecord& record);
[[nodiscard]] Result<secrets::SecretTarget> secret_target(const api::SecretTarget& target);
[[nodiscard]] api::SecretTarget secret_target(const secrets::SecretTarget& target);
[[nodiscard]] api::SecretStore secret_store(secrets::SecretLocation location);
[[nodiscard]] api::BackendState backend_state(backend::BackendPhase phase);
[[nodiscard]] api::BackendTarget backend_target(const backend::BackendTarget& target);
[[nodiscard]] Result<backend::BackendTarget> backend_target(const api::BackendTarget& target);
[[nodiscard]] api::BackendAccount backend_account(const backend::BackendAccount& account);

// Sessions.
[[nodiscard]] api::SessionSummary session_summary(const sessions::SessionInfo& info);
[[nodiscard]] api::SessionSummary session_summary(const sessions::SessionStateChanged& changed);
[[nodiscard]] api::ProcessRole process_role(sessions::ProcessRole role);
[[nodiscard]] api::EndReason end_reason(sessions::StopReason reason);

// Host.
[[nodiscard]] api::HostProfile host_profile(const host::HostProfile& profile, bool has_password);
[[nodiscard]] Result<host::HostProfile> host_profile(const api::HostProfile& profile);
[[nodiscard]] api::HostPhaseChanged host_phase(const host::HostPhaseChanged& changed);
[[nodiscard]] api::HostListening host_listening(const host::HostListening& listening);
[[nodiscard]] api::MatchEvent match_event(const host::MatchEvent& event);
[[nodiscard]] api::Player player(const gameserver::Player& player);
[[nodiscard]] api::PlayerEvent player_event(const host::PlayerEvent& event);
[[nodiscard]] api::ReachabilityChanged reachability(const std::optional<publish::ReachabilityChanged>& changed);
[[nodiscard]] api::PortMappingChanged port_mapping(const std::vector<net::PortMapping>& mappings,
                                                   const std::optional<Diagnostic>& failure);
[[nodiscard]] api::PublishStateChanged publish_state(const std::optional<publish::PublishState>& state);
[[nodiscard]] api::HostStatus host_status(const host::HostSnapshot& snapshot);
[[nodiscard]] Result<host::HostBan> host_ban(const api::Ban& ban);

// Browser and join.
[[nodiscard]] api::ServerEntry server_entry(const browser::ServerRow& row, std::optional<BuildId> compatible, bool own);
[[nodiscard]] api::ConnectionState connection_state(browser::ConnectionState state);
[[nodiscard]] Result<browser::ViewSpec> view_spec(const api::ViewSpec& spec);
[[nodiscard]] api::JoinTarget join_target(const browser::JoinTarget& target);

// Integration, logs, updates.
[[nodiscard]] api::IntegrationStatusEntry integration_entry(const integration::EntryStatus& entry);
[[nodiscard]] api::Prerequisite prerequisite(const integration::Prerequisite& prerequisite);
[[nodiscard]] api::LogEntry log_entry(const logging::LogEntry& entry);
[[nodiscard]] api::UpdateInfo update_info(const updates::UpdateOffer& offer);
[[nodiscard]] api::UpdatePhase update_phase(updates::UpdatePhase phase);

// Support.
[[nodiscard]] api::SupportQueryResponse support_answer(const support::SupportQuery& query,
                                                       const support::SupportVerdict& verdict);

}  // namespace rb::engine::convert
