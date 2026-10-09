#include "api_convert.hpp"

#include <algorithm>
#include <string>
#include <utility>
#include <variant>

#include "host_platform.hpp"
#include "messages.hpp"
#include "reboot/components/component_ref.hpp"
#include "reboot/components/manifest_platform.hpp"
#include "reboot/contracts/common.hpp"
#include "reboot/identity/display_name.hpp"
#include "reboot/injection/net_mode.hpp"
#include "reboot/support/support_reason.hpp"
#include "reboot/support/support_tier.hpp"
#include "reboot/ux/app_links.hpp"
#include "reboot/ux/onboarding_step.hpp"

namespace rb::engine::convert {

namespace {

template <class To, class From>
[[nodiscard]] constexpr To same_order(From value) noexcept {
    return static_cast<To>(static_cast<std::underlying_type_t<To>>(std::to_underlying(value)));
}

static_assert(std::to_underlying(api::HostPhase::Failed) == std::to_underlying(host::HostPhase::Failed));
static_assert(std::to_underlying(api::SessionPhase::Ended) == std::to_underlying(sessions::SessionPhase::Ended));
static_assert(std::to_underlying(api::ProcessRole::Winhost) == std::to_underlying(sessions::ProcessRole::Winhost));
static_assert(std::to_underlying(api::BackendState::Failed) == std::to_underlying(backend::BackendPhase::Failed));
static_assert(std::to_underlying(api::IntegrationState::Unknown) == std::to_underlying(integration::EntryState::Unknown));
static_assert(std::to_underlying(api::VersionSource::User) == std::to_underlying(builds::VersionSource::User));
static_assert(std::to_underlying(api::RemediationStep::ExplainSmartAppControl) ==
              std::to_underlying(components::RemediationStep::ExplainSmartAppControl));
static_assert(std::to_underlying(api::RecoveryState::RefetchFailed) ==
              std::to_underlying(components::RecoveryState::RefetchFailed));
static_assert(std::to_underlying(api::MatchEndAction::Nothing) == std::to_underlying(host::MatchEndAction::None));
static_assert(std::to_underlying(api::MatchState::Ended) == std::to_underlying(host::HostMatchState::Ended));
static_assert(std::to_underlying(api::SocketRole::Beacon) == std::to_underlying(contracts::game_server::SocketRole::Beacon));
static_assert(std::to_underlying(api::Region::SouthAmerica) == std::to_underlying(browser::Region::SouthAmerica));
static_assert(std::to_underlying(api::PasswordFilter::Only) == std::to_underlying(browser::PasswordFilter::Only));
static_assert(std::to_underlying(api::ServerSort::Name) == std::to_underlying(browser::ServerSort::Name));
static_assert(std::to_underlying(api::EnginePhase::Updating) == std::to_underlying(EnginePhase::Updating));
static_assert(std::to_underlying(api::LogLevel::Error) == std::to_underlying(LogLevel::Error));
static_assert(std::to_underlying(api::LogCategory::Ui) == std::to_underlying(LogCategory::Ui));
static_assert(std::to_underlying(api::StorageMode::InMemory) == std::to_underlying(storage::StorageMode::InMemory));
static_assert(std::to_underlying(api::CatalogSource::Bundled) == std::to_underlying(catalog::CatalogOrigin::Bundled));
static_assert(std::to_underlying(api::CatalogEntryStatus::Withdrawn) ==
              std::to_underlying(catalog::Availability::Withdrawn));
static_assert(std::to_underlying(api::IntegrationItem::DesktopEntry) == std::to_underlying(ports::IntegrationKind::DesktopEntry));
static_assert(std::to_underlying(api::BackendKind::Remote) == std::to_underlying(storage::BackendKind::Remote));
static_assert(std::to_underlying(api::AccountKind::Host) == std::to_underlying(contracts::backend::AccountRole::Host));
static_assert(std::to_underlying(api::SessionKind::Host) == std::to_underlying(sessions::SessionKind::Host));
static_assert(std::to_underlying(api::HostUpdatePolicy::Manual) == std::to_underlying(storage::HostUpdatePolicy::Manual));

[[nodiscard]] api::Listing listing(storage::HostListing listing) {
    return listing == storage::HostListing::Listed ? api::Listing::Listed : api::Listing::Unlisted;
}

[[nodiscard]] storage::HostListing listing(api::Listing listing) {
    return listing == api::Listing::Listed ? storage::HostListing::Listed : storage::HostListing::Unlisted;
}

[[nodiscard]] api::DiagnosticArg arg(const std::string& name, const Arg& value) {
    return api::DiagnosticArg{name, static_cast<api::ArgKind>(value.index()), contracts::common::detail::arg_text(value)};
}

[[nodiscard]] api::NoticeLevel notice_level(Severity severity) {
    switch (severity) {
        case Severity::Info: return api::NoticeLevel::Info;
        case Severity::Warning: return api::NoticeLevel::Warning;
        case Severity::Error: return api::NoticeLevel::Error;
    }
    return api::NoticeLevel::Info;
}

[[nodiscard]] std::string_view app_link_id(ux::AppLink link) {
    switch (link) {
        case ux::AppLink::BugReport: return "bug_report";
        case ux::AppLink::Releases: return "releases";
        case ux::AppLink::Discord: return "discord";
    }
    return "";
}

[[nodiscard]] api::Ban ban(const host::HostBan& ban) {
    api::Ban out;
    if (ban.address) out.address = ban.address->to_string();
    out.account_id = ban.account_id;
    out.reason = ban.reason;
    out.created_unix_ms = unix_ms(ban.created);
    if (ban.expires) out.expires_unix_ms = unix_ms(*ban.expires);
    return out;
}

}  // namespace

api::Diagnostic diagnostic(const Diagnostic& diag) {
    const contracts::common::WireDiagnostic wire = contracts::common::to_wire(diag);
    api::Diagnostic out;
    out.id = wire.id;
    out.args.reserve(wire.args.size());
    for (const contracts::common::WireArg& wire_arg : wire.args)
        out.args.push_back(api::DiagnosticArg{wire_arg.name, static_cast<api::ArgKind>(wire_arg.kind), wire_arg.value});
    out.detail = wire.detail;
    out.os_origin = static_cast<api::OsErrorOrigin>(wire.os_origin);
    out.os_code = wire.os_code;
    out.retryable = wire.retryable;
    out.kind = static_cast<api::ErrorKind>(wire.kind);
    return out;
}

std::vector<api::Diagnostic> diagnostics(const std::vector<Diagnostic>& diags) {
    std::vector<api::Diagnostic> out;
    out.reserve(diags.size());
    for (const Diagnostic& diag : diags) out.push_back(diagnostic(diag));
    return out;
}

api::Path path(const NativePath& native) {
    WirePath wire = to_wire(native);
    return api::Path{std::move(wire.display), std::move(wire.native)};
}

Result<NativePath> path(const api::Path& wire, std::string_view field) {
    Result<NativePath> native = from_wire(WirePath{wire.display, wire.native});
    if (!native) {
        Diagnostic error = invalid_request(field);
        error.causes.push_back(std::move(native.error()));
        return std::unexpected(std::move(error));
    }
    return native;
}

api::SemVer semver(const SemVer& version) { return api::SemVer{version.major, version.minor, version.patch, version.pre}; }

api::GameVersion game_version(const GameVersion& version) {
    api::GameVersion out{version.major, version.minor, std::nullopt};
    if (version.patch) out.patch = *version.patch;
    return out;
}

Result<GameVersion> game_version(const api::GameVersion& version, std::string_view field) {
    std::string text = std::to_string(version.major) + '.' + std::to_string(version.minor);
    if (version.patch) text += '.' + std::to_string(*version.patch);
    Result<GameVersion> parsed = GameVersion::parse(text);
    if (!parsed) {
        Diagnostic error = invalid_request(field);
        error.causes.push_back(std::move(parsed.error()));
        return std::unexpected(std::move(error));
    }
    return parsed;
}

u64 unix_ms(std::chrono::system_clock::time_point time) {
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(time.time_since_epoch()).count();
    return ms < 0 ? 0 : static_cast<u64>(ms);
}

std::chrono::system_clock::time_point from_unix_ms(u64 ms) {
    return std::chrono::system_clock::time_point(
        std::chrono::duration_cast<std::chrono::system_clock::duration>(std::chrono::milliseconds{static_cast<i64>(ms)}));
}

Diagnostic invalid_request(std::string_view field) {
    return make_diag(ErrorDomain::Engine, msg::kInvalidRequest).arg("field", field).kind(ErrorKind::InvalidInput);
}

api::RunnerKind runner(ports::RunnerKind kind) {
    switch (kind) {
        case ports::RunnerKind::Native: return api::RunnerKind::Native;
        case ports::RunnerKind::Umu: return api::RunnerKind::Umu;
        case ports::RunnerKind::Wine: return api::RunnerKind::Wine;
        case ports::RunnerKind::MacRuntime: return api::RunnerKind::MacRuntime;
    }
    return api::RunnerKind::Native;
}

ports::RunnerKind runner(api::RunnerKind kind) {
    switch (kind) {
        case api::RunnerKind::Native: return ports::RunnerKind::Native;
        case api::RunnerKind::Umu: return ports::RunnerKind::Umu;
        case api::RunnerKind::Wine: return ports::RunnerKind::Wine;
        case api::RunnerKind::MacRuntime: return ports::RunnerKind::MacRuntime;
    }
    return ports::RunnerKind::Native;
}

api::GameRole role(support::SupportRole role) {
    return role == support::SupportRole::Host ? api::GameRole::Host : api::GameRole::Client;
}

support::SupportRole role(api::GameRole role) {
    return role == api::GameRole::Host ? support::SupportRole::Host : support::SupportRole::Play;
}

api::SupportTier tier(support::SupportTier tier) {
    switch (tier) {
        case support::SupportTier::Tested: return api::SupportTier::Tested;
        case support::SupportTier::Untested: return api::SupportTier::Untested;
        case support::SupportTier::Blocked: return api::SupportTier::Blocked;
    }
    return api::SupportTier::Unknown;
}

api::CancelReason cancel_reason(CancelReason reason) { return same_order<api::CancelReason>(reason); }

namespace {

struct KindPair {
    EventKind domain;
    api::EventKind wire;
};

constexpr std::array<KindPair, 42> kKinds{{
    {EventKind::OpProgress, api::EventKind::OpProgress},
    {EventKind::OpCompleted, api::EventKind::OpCompleted},
    {EventKind::EngineState, api::EventKind::EngineState},
    {EventKind::SettingsChanged, api::EventKind::SettingsChanged},
    {EventKind::LibraryChanged, api::EventKind::LibraryChanged},
    {EventKind::CatalogChanged, api::EventKind::CatalogChanged},
    {EventKind::ComponentChanged, api::EventKind::ComponentChanged},
    {EventKind::IdentityChanged, api::EventKind::IdentityChanged},
    {EventKind::SessionStateChanged, api::EventKind::SessionStateChanged},
    {EventKind::SessionSpawned, api::EventKind::SessionSpawned},
    {EventKind::SessionDegraded, api::EventKind::SessionDegraded},
    {EventKind::SessionEnded, api::EventKind::SessionEnded},
    {EventKind::HostPhaseChanged, api::EventKind::HostPhaseChanged},
    {EventKind::HostListening, api::EventKind::HostListening},
    {EventKind::ReachabilityChanged, api::EventKind::ReachabilityChanged},
    {EventKind::PortMappingChanged, api::EventKind::PortMappingChanged},
    {EventKind::PublishStateChanged, api::EventKind::PublishStateChanged},
    {EventKind::MatchEvent, api::EventKind::MatchEvent},
    {EventKind::PlayerEvent, api::EventKind::PlayerEvent},
    {EventKind::BackendStateChanged, api::EventKind::BackendStateChanged},
    {EventKind::XmppUnavailable, api::EventKind::XmppUnavailable},
    {EventKind::BrowserConnectionChanged, api::EventKind::BrowserConnectionChanged},
    {EventKind::ViewSnapshot, api::EventKind::ViewSnapshot},
    {EventKind::ViewDelta, api::EventKind::ViewDelta},
    {EventKind::JoinTargetChanged, api::EventKind::JoinTargetChanged},
    {EventKind::UpdateAvailable, api::EventKind::UpdateAvailable},
    {EventKind::UpdateStaged, api::EventKind::UpdateStaged},
    {EventKind::EngineUpdating, api::EventKind::EngineUpdating},
    {EventKind::UpdateFailed, api::EventKind::UpdateFailed},
    {EventKind::NoticeAdded, api::EventKind::NoticeAdded},
    {EventKind::LogLine, api::EventKind::LogLine},
    {EventKind::UserActionRequired, api::EventKind::UserActionRequired},
    {EventKind::UserActionResolved, api::EventKind::UserActionResolved},
    {EventKind::OpStarted, api::EventKind::OpStarted},
    {EventKind::HostProfilesChanged, api::EventKind::HostProfilesChanged},
    {EventKind::BackendAccountsChanged, api::EventKind::BackendAccountsChanged},
    {EventKind::NoticeRemoved, api::EventKind::NoticeRemoved},
    {EventKind::OnboardingChanged, api::EventKind::OnboardingChanged},
    {EventKind::IntegrationChanged, api::EventKind::IntegrationChanged},
    {EventKind::PrerequisitesChanged, api::EventKind::PrerequisitesChanged},
    {EventKind::SecretStateChanged, api::EventKind::SecretStateChanged},
    {EventKind::StorageModeChanged, api::EventKind::StorageModeChanged},
}};

}  // namespace

std::optional<api::EventKind> event_kind(EventKind kind) {
    for (const KindPair& pair : kKinds)
        if (pair.domain == kind) return pair.wire;
    return std::nullopt;
}

std::optional<EventKind> event_kind(api::EventKind kind) {
    for (const KindPair& pair : kKinds)
        if (pair.wire == kind) return pair.domain;
    return std::nullopt;
}

api::EnginePhase engine_phase(EnginePhase phase) { return same_order<api::EnginePhase>(phase); }

api::EngineState engine_state(const EngineStateEvent& state) {
    return api::EngineState{engine_phase(state.phase), state.sessions, state.operations};
}

api::StorageModeChanged storage_mode_changed(const storage::StorageModeChanged& changed) {
    api::StorageModeChanged out;
    out.document = changed.document;
    out.mode = same_order<api::StorageMode>(changed.mode);
    if (changed.reason) out.reason = diagnostic(*changed.reason);
    return out;
}

api::Progress progress(const OpProgressEvent& progress, u32 method_id) {
    api::Progress out;
    out.op_id = progress.op.value;
    out.method_id = method_id;
    out.phase = progress.phase;
    out.done = progress.done;
    out.total = progress.total;
    out.rate_per_s = progress.rate_per_s;
    if (progress.eta) out.eta_s = static_cast<u32>(std::clamp<i64>(progress.eta->count(), 0, 0xFFFFFFFF));
    if (progress.awaiting_user) out.awaiting_request = progress.awaiting_user->value;
    return out;
}

api::MessageText message_text(MessageId id, const std::vector<std::pair<std::string, Arg>>& args) {
    api::MessageText out;
    out.id = std::string(id.id);
    for (const auto& [name, value] : args) out.args.push_back(arg(name, value));
    return out;
}

namespace {

constexpr std::string_view kBackgroundNoticePrefix = "background_failure:";

}  // namespace

api::NoticeKey background_notice_key(logging::BackgroundFailureId id, const std::optional<SessionId>& session) {
    api::NoticeKey key;
    key.kind = std::string(kBackgroundNoticePrefix) + std::to_string(id.value);
    if (session) key.session = convert::id(*session);
    return key;
}

std::optional<logging::BackgroundFailureId> background_failure_of(const api::NoticeKey& key) {
    if (!key.kind.starts_with(kBackgroundNoticePrefix)) return std::nullopt;
    const std::string_view digits = std::string_view(key.kind).substr(kBackgroundNoticePrefix.size());
    if (digits.empty() || digits.size() > 19) return std::nullopt;
    u64 value = 0;
    for (const char c : digits) {
        if (c < '0' || c > '9') return std::nullopt;
        value = value * 10 + static_cast<u64>(c - '0');
    }
    return logging::BackgroundFailureId{value};
}

api::Notice background_notice(const logging::BackgroundFailure& failure) {
    api::Notice notice;
    notice.key = background_notice_key(failure.id, failure.session);
    notice.level = notice_level(failure.diag.severity);
    notice.title = message_text(MessageId{failure.diag.id}, failure.diag.args);
    notice.created_unix_ms = unix_ms(failure.first_at);
    notice.dismissible = true;
    return notice;
}

api::NoticeKey notice_key(const ux::NoticeKey& key) {
    api::NoticeKey out;
    out.kind = std::string(ux::persisted_name(key.kind));
    if (key.session) out.session = id(*key.session);
    return out;
}

api::SuggestedAction suggested_action(const ux::SuggestedAction& action) {
    api::SuggestedAction out;
    out.label = std::string(ux::action_label(action).id);
    std::visit(
        [&out]<class A>(const A& alternative) {
            if constexpr (std::is_same_v<A, ux::RemediatePrerequisite>)
                out.remediate_prerequisite = api::RemediatePrerequisiteAction{alternative.prerequisite_id};
            else if constexpr (std::is_same_v<A, ux::InstallBuild>)
                out.install_build = api::InstallBuildAction{};
            else if constexpr (std::is_same_v<A, ux::ImportBuild>)
                out.import_build = api::ImportBuildAction{};
            else if constexpr (std::is_same_v<A, ux::EditDisplayName>)
                out.edit_display_name = api::EditDisplayNameAction{
                    alternative.role == contracts::backend::AccountRole::Host ? api::GameRole::Host : api::GameRole::Client};
            else if constexpr (std::is_same_v<A, ux::SetDefaultHostListing>)
                out.set_default_host_listing = api::SetDefaultHostListingAction{alternative.listed};
            else if constexpr (std::is_same_v<A, ux::ListHostProfile>)
                out.list_host_profile = api::ListHostProfileAction{id(alternative.profile)};
            else if constexpr (std::is_same_v<A, ux::CopyShareLink>)
                out.copy_share_link = api::CopyShareLinkAction{id(alternative.session)};
            else
                out.open_app_link = api::OpenAppLinkAction{std::string(app_link_id(alternative.link))};
        },
        action);
    return out;
}

api::Notice notice(const ux::Notice& notice) {
    api::Notice out;
    out.key = notice_key(notice.key);
    out.level = notice_level(notice.severity);
    out.title = message_text(notice.title.id, notice.title.args);
    out.body = message_text(notice.body.id, notice.body.args);
    for (const ux::SuggestedAction& action : notice.actions) out.actions.push_back(suggested_action(action));
    out.created_unix_ms = unix_ms(notice.created_at);
    out.dismissible = notice.dismissible;
    return out;
}

api::OnboardingState onboarding(const ux::OnboardingView& view) {
    api::OnboardingState out;
    for (const ux::OnboardingStep& step : view.steps) out.steps.emplace_back(ux::persisted_name(step.id));
    if (view.current) out.current = std::string(ux::persisted_name(*view.current));
    out.finished = view.status == ux::OnboardingStatus::Completed;
    out.skipped = view.status == ux::OnboardingStatus::Exited;
    return out;
}

api::Build build(const builds::InstalledBuild& build) {
    api::Build out;
    out.id = id(build.id);
    out.name = build.name;
    out.root = path(build.root);
    if (build.version) out.version = game_version(*build.version);
    if (build.cl) out.changelist = build.cl->value;
    if (build.version_source) out.version_source = same_order<api::VersionSource>(*build.version_source);
    out.catalog_entry = build.catalog_entry;
    out.added_unix_ms = unix_ms(build.added_at);
    out.present = build.presence != builds::BuildPresence::Missing;
    return out;
}

api::CatalogEntry catalog_entry(const catalog::CatalogEntry& entry, bool installed) {
    api::CatalogEntry out;
    out.id = entry.id;
    out.version = game_version(entry.version);
    if (entry.changelist) out.changelist = entry.changelist->value;
    out.display_name = entry.display_name.empty() ? entry.id : entry.display_name;
    out.status = same_order<api::CatalogEntryStatus>(entry.availability);
    if (entry.availability == catalog::Availability::Available && !entry.installable())
        out.status = api::CatalogEntryStatus::Unavailable;
    out.archive_size = entry.archive_size;
    out.installed_size = entry.installed_size;
    out.installed = installed;
    return out;
}

api::CatalogSource catalog_source(catalog::CatalogOrigin origin) { return same_order<api::CatalogSource>(origin); }

api::InstallSuggestDestinationResponse destination(const builds::DestinationSuggestion& suggestion) {
    api::InstallSuggestDestinationResponse out;
    out.destination = path(suggestion.destination);
    out.name = suggestion.name;
    out.required_bytes = suggestion.required_bytes;
    for (const builds::VolumeCandidate& candidate : suggestion.volumes) {
        api::Volume volume;
        volume.root = path(candidate.volume.mount);
        volume.label = candidate.volume.label;
        volume.free_bytes = candidate.volume.free_bytes;
        volume.total_bytes = candidate.volume.total_bytes;
        if (candidate.problem) volume.problem = diagnostic(*candidate.problem);
        out.volumes.push_back(std::move(volume));
    }
    return out;
}

api::ComponentProblem component_problem(const components::ComponentProblem& problem) {
    api::ComponentProblem out;
    out.reason = diagnostic(components::to_diagnostic(problem));
    out.file = path(problem.file);
    for (const components::RemediationStep step : problem.remediation)
        out.remediation.push_back(same_order<api::RemediationStep>(step));
    out.recovery = same_order<api::RecoveryState>(problem.recovery);
    if (problem.refetch_error) out.refetch_error = diagnostic(*problem.refetch_error);
    return out;
}

api::Component component(const std::vector<components::ComponentInfo>& versions,
                         const std::optional<components::ComponentProblem>& problem) {
    api::Component out;
    if (versions.empty()) return out;
    out.id = versions.front().ref.id;
    out.kind = versions.front().ref.kind == components::ComponentKind::Payload ? api::ComponentKind::Payload
                                                                             : api::ComponentKind::Runtime;
    const components::ComponentInfo* selected = nullptr;
    const components::ComponentInfo* ready = nullptr;
    for (const components::ComponentInfo& info : versions) {
        if (info.selected) selected = &info;
        if (info.state == components::ComponentState::Ready && (ready == nullptr || info.last_good)) ready = &info;
        if (info.pins > 0) out.in_use = true;
    }
    if (selected != nullptr && selected->state == components::ComponentState::Ready) ready = selected;
    const components::ComponentInfo& shown = ready != nullptr ? *ready : (selected != nullptr ? *selected : versions.front());
    out.version = shown.ref.version;
    out.size_bytes = shown.size_bytes;
    if (selected == nullptr) {
        out.state = ready != nullptr ? api::ComponentState::Installed : api::ComponentState::Missing;
    } else {
        switch (selected->state) {
            case components::ComponentState::Ready: out.state = api::ComponentState::Installed; break;
            case components::ComponentState::Fetching: out.state = api::ComponentState::Installing; break;
            case components::ComponentState::Broken: out.state = api::ComponentState::Broken; break;
            case components::ComponentState::Missing:
                out.state = ready != nullptr ? api::ComponentState::UpdateAvailable : api::ComponentState::Missing;
                break;
        }
        if (ready != nullptr && ready != selected) out.available_version = selected->ref.version;
    }
    if (problem) {
        out.problem = component_problem(*problem);
        out.state = api::ComponentState::Broken;
    }
    return out;
}

std::vector<api::Component> components(const std::vector<components::ComponentInfo>& all) {
    std::vector<std::vector<components::ComponentInfo>> grouped;
    for (const components::ComponentInfo& info : all) {
        auto it = std::ranges::find_if(grouped, [&info](const auto& group) { return group.front().ref.id == info.ref.id; });
        if (it == grouped.end()) grouped.push_back({info});
        else it->push_back(info);
    }
    std::vector<api::Component> out;
    out.reserve(grouped.size());
    for (const auto& group : grouped) out.push_back(component(group, std::nullopt));
    return out;
}

api::IdentityProfile identity_profile(const identity::AccountRecord& record) {
    api::IdentityProfile out;
    out.role = record.role == contracts::backend::AccountRole::Host ? api::GameRole::Host : api::GameRole::Client;
    out.display_name = record.display_name;
    out.account_id = identity::account_id(record);
    out.is_default = identity::is_default_display_name(record.display_name, record.role);
    return out;
}

Result<secrets::SecretTarget> secret_target(const api::SecretTarget& target) {
    // The scope a kind does not take is fed to SecretTarget::parse, which refuses it as secrets.invalid_scope.
    std::string scope;
    if (target.host_profile) scope = secrets::SecretScope::host_profile(id(*target.host_profile)).text();
    else if (target.backend) {
        HostPort endpoint{target.backend->host, std::nullopt};
        if (target.backend->port) {
            if (*target.backend->port == 0 || *target.backend->port > 0xFFFF) return std::unexpected(invalid_request("target.backend.port"));
            endpoint.port = Port{static_cast<u16>(*target.backend->port)};
        }
        scope = secrets::SecretScope::backend(endpoint).text();
    } else if (target.join_request) {
        scope = secrets::SecretScope::join_request(RequestId{*target.join_request}).text();
    }
    secrets::SecretKind kind{};
    switch (target.kind) {
        case api::SecretKind::HostJoinPassword:
            kind = secrets::SecretKind::HostJoinPassword;
            if (!target.host_profile) scope.clear();
            break;
        case api::SecretKind::BackendPassword:
            kind = secrets::SecretKind::RemoteBackendPassword;
            if (!target.backend) scope.clear();
            break;
        case api::SecretKind::JoinPassword:
            kind = secrets::SecretKind::JoinPassword;
            if (!target.join_request) scope.clear();
            break;
        case api::SecretKind::Unspecified:
        default: return std::unexpected(invalid_request("target.kind"));
    }
    return secrets::SecretTarget::parse(kind, scope);
}

api::SecretTarget secret_target(const secrets::SecretTarget& target) {
    api::SecretTarget out;
    const std::string& text = target.scope.text();
    switch (target.kind) {
        case secrets::SecretKind::HostJoinPassword:
            out.kind = api::SecretKind::HostJoinPassword;
            if (Result<Uuid> uuid = parse_uuid(text)) out.host_profile = api::HostProfileId{*uuid};
            break;
        case secrets::SecretKind::RemoteBackendPassword: {
            out.kind = api::SecretKind::BackendPassword;
            api::BackendHost host;
            if (auto parsed = parse_host_port(text)) {
                host.host = parsed->host;
                if (parsed->port) host.port = parsed->port->value;
            } else {
                host.host = text;
            }
            out.backend = std::move(host);
            break;
        }
        case secrets::SecretKind::JoinPassword: {
            out.kind = api::SecretKind::JoinPassword;
            u64 request = 0;
            for (const char c : text) {
                if (c < '0' || c > '9') break;
                request = request * 10 + static_cast<u64>(c - '0');
            }
            out.join_request = request;
            break;
        }
    }
    return out;
}

api::SecretStore secret_store(secrets::SecretLocation location) {
    switch (location) {
        case secrets::SecretLocation::OsStore:
            if constexpr (kWindowsHost) return api::SecretStore::CredentialManager;
            else if constexpr (kHostOs == components::ManifestOs::MacOs) return api::SecretStore::Keychain;
            else return api::SecretStore::Libsecret;
        case secrets::SecretLocation::FileStore:
            if constexpr (kWindowsHost) return api::SecretStore::DpapiFile;
            else return api::SecretStore::OwnerOnlyFile;
        case secrets::SecretLocation::Absent:
        case secrets::SecretLocation::Session: break;
    }
    return api::SecretStore::Unavailable;
}

api::BackendState backend_state(backend::BackendPhase phase) { return same_order<api::BackendState>(phase); }

api::BackendTarget backend_target(const backend::BackendTarget& target) {
    api::BackendTarget out;
    out.kind = same_order<api::BackendKind>(target.kind());
    if (const auto* local = std::get_if<backend::LocalBackend>(&target.value)) {
        out.origin = "http://" + local->endpoint.host;
        if (local->endpoint.port) out.origin += ':' + std::to_string(local->endpoint.port->value);
    } else if (const std::optional<backend::BackendUrl> url = target.upstream_url()) {
        out.origin = url->origin();
    }
    return out;
}

Result<backend::BackendTarget> backend_target(const api::BackendTarget& target) {
    switch (target.kind) {
        case api::BackendKind::Embedded: return backend::BackendTarget{backend::EmbeddedBackend{}};
        case api::BackendKind::Local: {
            Result<backend::BackendUrl> url = backend::BackendUrl::parse(target.origin);
            if (!url) return std::unexpected(std::move(url.error()));
            return backend::BackendTarget{backend::LocalBackend{url->endpoint(), std::nullopt}};
        }
        case api::BackendKind::Remote: {
            Result<backend::BackendUrl> url = backend::BackendUrl::parse(target.origin);
            if (!url) return std::unexpected(std::move(url.error()));
            return backend::BackendTarget{backend::RemoteBackend{std::move(*url), std::nullopt}};
        }
    }
    return std::unexpected(invalid_request("target.kind"));
}

api::BackendAccount backend_account(const backend::BackendAccount& account) {
    api::BackendAccount out;
    out.account_id = account.account_id;
    out.display_name = account.display_name;
    out.kind = same_order<api::AccountKind>(account.role);
    if (account.last_login) out.last_login_unix_ms = unix_ms(*account.last_login);
    return out;
}

api::SessionSummary session_summary(const sessions::SessionInfo& info) {
    api::SessionSummary out;
    out.id = id(info.id);
    out.kind = same_order<api::SessionKind>(info.kind);
    out.phase = same_order<api::SessionPhase>(info.phase);
    if (info.parent) out.parent = id(*info.parent);
    out.started_unix_ms = unix_ms(info.started_at);
    out.label = info.label;
    return out;
}

api::SessionSummary session_summary(const sessions::SessionStateChanged& changed) {
    api::SessionSummary out;
    out.id = id(changed.session);
    out.kind = same_order<api::SessionKind>(changed.kind);
    out.phase = same_order<api::SessionPhase>(changed.phase);
    if (changed.parent) out.parent = id(*changed.parent);
    out.started_unix_ms = unix_ms(changed.started_at);
    out.label = changed.label;
    return out;
}

api::ProcessRole process_role(sessions::ProcessRole role) { return same_order<api::ProcessRole>(role); }

api::EndReason end_reason(sessions::StopReason reason) {
    switch (reason) {
        case sessions::StopReason::LeaseEnded: return api::EndReason::LeaseEnded;
        case sessions::StopReason::EngineShutdown:
        case sessions::StopReason::Update:
        case sessions::StopReason::Replaced: return api::EndReason::EngineShutdown;
        case sessions::StopReason::Exited: return api::EndReason::Exited;
        case sessions::StopReason::Crashed: return api::EndReason::Crashed;
        case sessions::StopReason::Unresponsive:
        case sessions::StopReason::LaunchFailed:
        case sessions::StopReason::Fatal: return api::EndReason::Failed;
        case sessions::StopReason::User:
        case sessions::StopReason::ParentEnded:
        case sessions::StopReason::BuildRemoved:
        case sessions::StopReason::MatchEnded: return api::EndReason::Stopped;
    }
    return api::EndReason::Stopped;
}

api::HostProfile host_profile(const host::HostProfile& profile, bool has_password) {
    api::HostProfile out;
    out.id = id(profile.id);
    out.revision = profile.revision;
    out.name = profile.name;
    if (profile.build) out.build = id(*profile.build);
    if (profile.version) out.version = api::HostVersion{game_version(profile.version->version), profile.version->cl.value};
    if (const auto* pinned = std::get_if<host::PinnedPorts>(&profile.port)) {
        out.port.pinned = pinned->first.value;
    } else {
        const auto& automatic = std::get<host::AutoPorts>(profile.port);
        out.port.range = api::PortRange{automatic.range.first.value, automatic.range.last.value};
    }
    out.port_mapping = profile.port_mapping;
    out.listing = listing(profile.listing);
    out.server_name = profile.server_name;
    out.description = profile.description;
    out.has_password = has_password;
    out.max_players = profile.match.max_players;
    out.playlist = profile.match.playlist;
    if (const auto* at_players = std::get_if<gameserver::AutoAtPlayers>(&profile.match.start))
        out.start.auto_at_players = at_players->players;
    else
        out.start.manual = true;
    out.tick_rate = profile.match.tick_rate;
    out.match_end.action = same_order<api::MatchEndAction>(profile.match_end.action);
    out.match_end.delay_s = static_cast<u32>(std::max<i64>(profile.match_end.delay.count(), 0));
    for (const host::IpCidr& cidr : profile.operators.operator_cidrs) out.operator_cidrs.push_back(cidr.to_string());
    for (const host::HostBan& entry : profile.operators.bans) out.bans.push_back(ban(entry));
    out.auto_server = profile.is_auto();
    if (profile.update_policy) out.update_policy = same_order<api::HostUpdatePolicy>(*profile.update_policy);
    return out;
}

Result<host::HostBan> host_ban(const api::Ban& ban) {
    host::HostBan out;
    if (ban.address) {
        Result<host::IpCidr> cidr = host::IpCidr::parse(*ban.address);
        if (!cidr) return std::unexpected(std::move(cidr.error()));
        out.address = *cidr;
    }
    out.account_id = ban.account_id;
    out.reason = ban.reason;
    out.created = from_unix_ms(ban.created_unix_ms);
    if (ban.expires_unix_ms) out.expires = from_unix_ms(*ban.expires_unix_ms);
    return out;
}

Result<host::HostProfile> host_profile(const api::HostProfile& profile) {
    host::HostProfile out;
    out.id = id(profile.id);
    out.revision = profile.revision;
    out.name = profile.name;
    if (profile.build) out.build = id(*profile.build);
    if (profile.version) {
        Result<GameVersion> version = game_version(profile.version->version, "profile.version");
        if (!version) return std::unexpected(std::move(version.error()));
        out.version = host::HostVersion{*version, Changelist{profile.version->changelist}};
    }
    if (profile.port.pinned) {
        if (*profile.port.pinned == 0 || *profile.port.pinned > 0xFFFF) return std::unexpected(invalid_request("profile.port"));
        out.port = host::PinnedPorts{Port{static_cast<u16>(*profile.port.pinned)}};
    } else if (profile.port.range) {
        const api::PortRange& range = *profile.port.range;
        if (range.first == 0 || range.first > 0xFFFF || range.last == 0 || range.last > 0xFFFF)
            return std::unexpected(invalid_request("profile.port"));
        out.port = host::AutoPorts{host::PortRange{Port{static_cast<u16>(range.first)}, Port{static_cast<u16>(range.last)}}};
    }
    out.port_mapping = profile.port_mapping;
    out.listing = listing(profile.listing);
    out.server_name = profile.server_name;
    out.description = profile.description;
    out.match.playlist = profile.playlist;
    out.match.max_players = profile.max_players;
    out.match.tick_rate = profile.tick_rate;
    if (profile.start.auto_at_players) out.match.start = gameserver::AutoAtPlayers{*profile.start.auto_at_players};
    else out.match.start = gameserver::ManualStart{};
    out.match_end.action = same_order<host::MatchEndAction>(profile.match_end.action);
    out.match_end.delay = std::chrono::seconds{profile.match_end.delay_s};
    for (const std::string& text : profile.operator_cidrs) {
        Result<host::IpCidr> cidr = host::IpCidr::parse(text);
        if (!cidr) return std::unexpected(std::move(cidr.error()));
        out.operators.operator_cidrs.push_back(*cidr);
    }
    for (const api::Ban& entry : profile.bans) {
        Result<host::HostBan> parsed = host_ban(entry);
        if (!parsed) return std::unexpected(std::move(parsed.error()));
        out.operators.bans.push_back(std::move(*parsed));
    }
    if (profile.update_policy) out.update_policy = same_order<storage::HostUpdatePolicy>(*profile.update_policy);
    return out;
}

api::HostPhaseChanged host_phase(const host::HostPhaseChanged& changed) {
    api::HostPhaseChanged out;
    out.phase = same_order<api::HostPhase>(changed.phase);
    if (changed.reason) out.reason = diagnostic(*changed.reason);
    return out;
}

api::HostListening host_listening(const host::HostListening& listening) {
    api::HostListening out;
    for (const gameserver::BoundSocket& bound : listening.bound)
        out.sockets.push_back(api::BoundSocket{same_order<api::SocketRole>(bound.role), bound.port});
    return out;
}

api::MatchEvent match_event(const host::MatchEvent& event) {
    api::MatchEvent out;
    out.state = same_order<api::MatchState>(event.state);
    if (event.result) {
        api::MatchResult result;
        switch (event.result->reason) {
            case gameserver::MatchEndReason::Completed: result.reason = "completed"; break;
            case gameserver::MatchEndReason::Aborted: result.reason = "aborted"; break;
            case gameserver::MatchEndReason::Operator: result.reason = "operator"; break;
        }
        result.winner = event.result->winner;
        for (const gameserver::Placement& placement : event.result->placements)
            result.placements.push_back(api::Placement{placement.account_id, placement.place});
        out.result = std::move(result);
    }
    if (event.fires_at) out.restart_at_unix_ms = unix_ms(*event.fires_at);
    return out;
}

api::Player player(const gameserver::Player& player) {
    return api::Player{player.account_id, player.display_name, player.address, player.player_id};
}

api::PlayerEvent player_event(const host::PlayerEvent& event) {
    api::PlayerEvent out;
    if (event.change == host::PlayerChange::Joined) out.joined = player(event.player);
    else out.left = player(event.player);
    out.player_count = event.player_count;
    return out;
}

api::ReachabilityChanged reachability(const std::optional<publish::ReachabilityChanged>& changed) {
    api::ReachabilityChanged out;
    out.reachability = api::Reachability::Unknown;
    if (!changed) return out;
    if (changed->status) {
        switch (*changed->status) {
            case publish::HostStatus::AwaitingProbe: out.reachability = api::Reachability::AwaitingProbe; break;
            case publish::HostStatus::Live: out.reachability = api::Reachability::Reachable; break;
            case publish::HostStatus::LiveUnreachable: out.reachability = api::Reachability::Unreachable; break;
        }
    }
    if (changed->public_endpoint) out.public_address = changed->public_endpoint->to_string();
    return out;
}

api::PortMappingChanged port_mapping(const std::vector<net::PortMapping>& mappings, const std::optional<Diagnostic>& failure) {
    api::PortMappingChanged out;
    out.protocol = api::MappingProtocol::Unmapped;
    if (!mappings.empty())
        out.protocol = mappings.front().method == net::MappingMethod::Upnp ? api::MappingProtocol::Upnp
                                                                          : api::MappingProtocol::NatPmp;
    for (const net::PortMapping& mapping : mappings) {
        out.ports.push_back(api::MappedPort{mapping.internal.value, mapping.external.value});
        out.lease_s = static_cast<u32>(std::max<i64>(mapping.lease.count(), 0));
    }
    if (failure) out.error = diagnostic(*failure);
    return out;
}

api::PublishStateChanged publish_state(const std::optional<publish::PublishState>& state) {
    api::PublishStateChanged out;
    out.state = api::PublishState::Unpublished;
    out.hidden = true;
    if (!state) return out;
    switch (state->phase) {
        case publish::PublishPhase::Connecting:
        case publish::PublishPhase::Registering:
        case publish::PublishPhase::Retrying: out.state = api::PublishState::Registering; break;
        case publish::PublishPhase::Registered: out.state = api::PublishState::Published; break;
        case publish::PublishPhase::Superseded:
        case publish::PublishPhase::Refused: out.state = api::PublishState::Failed; break;
        case publish::PublishPhase::Withdrawing: out.state = api::PublishState::Unpublished; break;
    }
    out.server = id(state->server);
    out.hidden = state->hidden;
    if (state->error) out.error = diagnostic(*state->error);
    return out;
}

api::HostStatus host_status(const host::HostSnapshot& snapshot) {
    api::HostStatus out;
    out.phase = host_phase(snapshot.phase);
    if (snapshot.listening) out.listening = host_listening(*snapshot.listening);
    out.reachability = reachability(snapshot.reachability);
    out.mapping = port_mapping(snapshot.mappings, std::nullopt);
    out.publish = publish_state(snapshot.publish);
    out.match = match_event(snapshot.match);
    for (const gameserver::Player& entry : snapshot.players) out.players.push_back(player(entry));
    return out;
}

api::ServerEntry server_entry(const browser::ServerRow& row, std::optional<BuildId> compatible, bool own) {
    api::ServerEntry out;
    out.id = id(row.id);
    out.name = row.name;
    out.author = row.author;
    out.version = row.version;
    out.bucket = row.bucket;
    out.players = row.players;
    out.max_players = row.max_players;
    out.region = same_order<api::Region>(row.region);
    out.has_password = row.has_password;
    out.reachable = row.reachable;
    out.online = row.online;
    out.created_unix_ms = unix_ms(row.created_at);
    if (compatible) out.compatible_build = id(*compatible);
    out.own = own;
    return out;
}

api::ConnectionState connection_state(browser::ConnectionState state) {
    switch (state) {
        case browser::ConnectionState::Idle: return api::ConnectionState::Idle;
        case browser::ConnectionState::Connecting: return api::ConnectionState::Connecting;
        case browser::ConnectionState::Connected: return api::ConnectionState::Connected;
        case browser::ConnectionState::Draining: return api::ConnectionState::Draining;
        case browser::ConnectionState::UdpBlocked: return api::ConnectionState::UdpBlocked;
        case browser::ConnectionState::Backoff:
        case browser::ConnectionState::Offline:
        case browser::ConnectionState::ServiceDown: return api::ConnectionState::Backoff;
    }
    return api::ConnectionState::Idle;
}

Result<browser::ViewSpec> view_spec(const api::ViewSpec& spec) {
    browser::ViewSpec out;
    if (spec.bucket != 0) out.versions.buckets.push_back(spec.bucket);
    out.password = same_order<browser::PasswordFilter>(spec.password);
    out.region = same_order<browser::Region>(spec.region);
    out.sort = same_order<browser::ServerSort>(spec.sort);
    out.window = spec.window == 0 ? browser::kSmallWindow : spec.window;
    if (Result<void> valid = out.validate(); !valid) return std::unexpected(std::move(valid.error()));
    return out;
}

api::JoinTarget join_target(const browser::JoinTarget& target) {
    api::JoinTarget out;
    if (const auto* server = std::get_if<browser::ServerTarget>(&target.target))
        out.server = api::ServerTarget{id(server->id), server->name, server->author};
    else
        out.address = std::get<browser::AddressTarget>(target.target).text;
    return out;
}

api::IntegrationStatusEntry integration_entry(const integration::EntryStatus& entry) {
    api::IntegrationStatusEntry out;
    out.item = same_order<api::IntegrationItem>(entry.kind);
    out.state = same_order<api::IntegrationState>(entry.state);
    if (entry.detail) out.detail = diagnostic(*entry.detail);
    out.declined = entry.declined;
    out.target = entry.target;
    return out;
}

api::Prerequisite prerequisite(const integration::Prerequisite& prerequisite) {
    api::Prerequisite out;
    out.id = std::string(integration::to_string(prerequisite.id));
    if (prerequisite.met) out.state = api::PrerequisiteState::Met;
    else if (prerequisite.impact == integration::PrerequisiteImpact::Advisory) out.state = api::PrerequisiteState::Degraded;
    else out.state = api::PrerequisiteState::Missing;
    out.remediable = prerequisite.remedy != integration::Remedy::None;
    if (!prerequisite.met) {
        Diagnostic detail = make_diag(ErrorDomain::Integration, prerequisite.platform_hint.value_or(prerequisite.guidance))
                                .severity(Severity::Warning);
        out.detail = diagnostic(detail);
    }
    return out;
}

api::LogEntry log_entry(const logging::LogEntry& entry) {
    api::LogEntry out;
    out.seq = entry.seq;
    out.unix_ms = unix_ms(entry.record.time);
    out.level = same_order<api::LogLevel>(entry.record.level);
    out.category = same_order<api::LogCategory>(entry.record.category);
    if (entry.record.session) out.session = id(*entry.record.session);
    out.text = entry.record.text;
    return out;
}

api::UpdateInfo update_info(const updates::UpdateOffer& offer) {
    api::UpdateInfo out;
    out.version = semver(offer.entry.version);
    out.size_bytes = offer.entry.package.size;
    out.required = offer.required;
    return out;
}

api::UpdatePhase update_phase(updates::UpdatePhase phase) {
    switch (phase) {
        case updates::UpdatePhase::Idle: return api::UpdatePhase::Idle;
        case updates::UpdatePhase::Checking: return api::UpdatePhase::Checking;
        case updates::UpdatePhase::Available: return api::UpdatePhase::Available;
        case updates::UpdatePhase::Downloading: return api::UpdatePhase::Downloading;
        case updates::UpdatePhase::Staged: return api::UpdatePhase::Staged;
        case updates::UpdatePhase::WaitingForIdle:
        case updates::UpdatePhase::Draining: return api::UpdatePhase::WaitingForIdle;
        case updates::UpdatePhase::Applying: return api::UpdatePhase::Applying;
        case updates::UpdatePhase::Failed: return api::UpdatePhase::Failed;
    }
    return api::UpdatePhase::Idle;
}

api::SupportQueryResponse support_answer(const support::SupportQuery& query, const support::SupportVerdict& verdict) {
    api::SupportQueryResponse out;
    out.tier = tier(verdict.tier);
    out.provider = verdict.provider == support::SupportProvider::Custom ? api::SupportProvider::Custom
                                                                         : api::SupportProvider::Ours;
    if (query.version) out.version = game_version(*query.version);
    for (const support::SupportReason reason : verdict.reasons)
        out.reasons.push_back(diagnostic(support::to_diagnostic(reason, query)));
    return out;
}

}  // namespace rb::engine::convert
