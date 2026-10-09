#include "api_requests.hpp"

#include <algorithm>
#include <string_view>
#include <utility>
#include <vector>

#include "api_convert.hpp"
#include "messages.hpp"
#include "reboot/backend/account_rename_conflict.hpp"
#include "reboot/backend/unencrypted_upstream_prompt.hpp"
#include "reboot/browser/join_prompts.hpp"
#include "reboot/browser/own_servers.hpp"
#include "reboot/builds/choose_version_prompt.hpp"
#include "reboot/builds/library.hpp"
#include "reboot/builds/user_version.hpp"
#include "reboot/catalog/catalog_service.hpp"
#include "reboot/compat/rosetta_install_answer.hpp"
#include "reboot/compat/rosetta_install_request.hpp"
#include "reboot/front/unencrypted_upstream_prompt.hpp"
#include "reboot/host/untested_host_prompt.hpp"
#include "reboot/play/play_prompts.hpp"
#include "reboot/secrets/needs_secret.hpp"
#include "reboot/sessions/session_registry.hpp"
#include "reboot/updates/update_prompts.hpp"

namespace rb::engine::requests {

namespace {

[[nodiscard]] std::string_view kind_name(UserRequestKind kind) noexcept {
    switch (kind) {
        case UserRequestKind::NeedsSecret: return "needs_secret";
        case UserRequestKind::ConfirmJoin: return "confirm_join";
        case UserRequestKind::NeedsJoinPassword: return "needs_join_password";
        case UserRequestKind::AutoServerConsent: return "auto_server_consent";
        case UserRequestKind::ConfirmUnencryptedUpstream: return "confirm_unencrypted_upstream";
        case UserRequestKind::AccountRenameConflict: return "account_rename_conflict";
        case UserRequestKind::RosettaInstall: return "rosetta_install";
        case UserRequestKind::AgentRequiresApproval: return "agent_requires_approval";
        case UserRequestKind::ChooseVersion: return "choose_version";
        case UserRequestKind::ConfirmUntested: return "confirm_untested";
        case UserRequestKind::ConfirmStopSessions: return "confirm_stop_sessions";
    }
    return "unknown";
}

[[nodiscard]] Diagnostic mismatch(const UserRequest& request) {
    return make_diag(ErrorDomain::Engine, msg::kAnswerMismatch)
        .arg("request", request.id.value)
        .arg("kind", kind_name(request.kind))
        .kind(ErrorKind::InvalidInput);
}

[[nodiscard]] api::ConfirmUntestedPrompt untested(const support::SupportQuery& query, const support::SupportVerdict& verdict) {
    api::ConfirmUntestedPrompt out;
    if (query.version) out.version = convert::game_version(*query.version);
    out.role = convert::role(query.role);
    out.runner = convert::runner(query.runner);
    out.reasons = convert::support_answer(query, verdict).reasons;
    return out;
}

template <class T>
[[nodiscard]] const T* payload_as(const UserRequest& request) {
    return std::any_cast<T>(&request.payload);
}

}  // namespace

api::ServerEntry server_entry(const ApiRouterDeps& deps, const browser::ServerRow& row) {
    std::optional<BuildId> compatible;
    if (const std::vector<builds::InstalledBuild> builds = deps.library.find_compatible(row.version); !builds.empty())
        compatible = builds.front().id;
    return convert::server_entry(row, compatible, deps.own_servers.owns(row.id));
}

api::UserActionRequired user_action(const ApiRouterDeps& deps, const UserRequest& request) {
    api::UserActionRequired out;
    out.request_id = request.id.value;
    out.kind = static_cast<api::UserRequestKind>(std::to_underlying(request.kind));
    if (request.op) out.op_id = request.op->value;
    if (request.session) out.session = convert::id(*request.session);

    api::RequestPrompt& prompt = out.prompt;
    switch (request.kind) {
        case UserRequestKind::NeedsSecret: {
            api::NeedsSecretPrompt needs;
            if (const auto* payload = payload_as<secrets::NeedsSecret>(request))
                needs.target = convert::secret_target(payload->target);
            prompt.needs_secret = std::move(needs);
            break;
        }
        case UserRequestKind::ConfirmJoin: {
            api::ConfirmJoinPrompt confirm;
            if (const auto* payload = payload_as<browser::ConfirmJoinPrompt>(request))
                confirm.server = server_entry(deps, payload->server.row);
            prompt.confirm_join = std::move(confirm);
            break;
        }
        case UserRequestKind::NeedsJoinPassword: {
            api::NeedsJoinPasswordPrompt needs;
            if (const auto* payload = payload_as<browser::NeedsJoinPasswordPrompt>(request)) {
                needs.server = server_entry(deps, payload->server);
                needs.retry = payload->retry;
            }
            prompt.needs_join_password = std::move(needs);
            break;
        }
        case UserRequestKind::AutoServerConsent: {
            api::AutoServerConsentPrompt consent;
            if (const auto* payload = payload_as<play::AutoServerConsentPrompt>(request))
                consent.version = convert::game_version(payload->version);
            prompt.auto_server_consent = std::move(consent);
            break;
        }
        case UserRequestKind::ConfirmUnencryptedUpstream: {
            api::ConfirmUnencryptedUpstreamPrompt confirm;
            if (const auto* backend_prompt = payload_as<backend::UnencryptedUpstreamPrompt>(request))
                confirm.origin = backend_prompt->origin;
            else if (const auto* front_prompt = payload_as<front::UnencryptedUpstreamPrompt>(request))
                confirm.origin = front_prompt->origin;
            prompt.confirm_unencrypted_upstream = std::move(confirm);
            break;
        }
        case UserRequestKind::AccountRenameConflict: {
            api::AccountRenameConflictPrompt conflict;
            if (const auto* payload = payload_as<backend::AccountRenameConflictPrompt>(request)) {
                conflict.account_id = payload->old_account_id;
                conflict.display_name = payload->new_account_id;
                conflict.conflicting_account_id = payload->new_account_id;
            }
            prompt.account_rename_conflict = std::move(conflict);
            break;
        }
        case UserRequestKind::RosettaInstall: prompt.rosetta_install = api::RosettaInstallPrompt{}; break;
        case UserRequestKind::AgentRequiresApproval:
            prompt.agent_requires_approval = api::AgentRequiresApprovalPrompt{};
            break;
        case UserRequestKind::ChooseVersion: {
            api::ChooseVersionPrompt choose;
            if (const auto* payload = payload_as<builds::ChooseVersionPrompt>(request)) {
                choose.build_root = convert::path(payload->root);
                choose.detected = payload->raw.value_or("");
            }
            std::vector<GameVersion> known;
            for (const catalog::CatalogEntry& entry : deps.catalog.list(catalog::CatalogFilter{.include_unavailable = true}))
                if (std::ranges::find(known, entry.version) == known.end()) known.push_back(entry.version);
            for (const GameVersion& version : known) choose.candidates.push_back(convert::game_version(version));
            prompt.choose_version = std::move(choose);
            break;
        }
        case UserRequestKind::ConfirmUntested: {
            if (const auto* host_prompt = payload_as<host::UntestedHostPrompt>(request))
                prompt.confirm_untested = untested(host_prompt->query, host_prompt->verdict);
            else if (const auto* play_prompt = payload_as<play::UntestedPlayPrompt>(request))
                prompt.confirm_untested = untested(play_prompt->query, play_prompt->verdict);
            else
                prompt.confirm_untested = api::ConfirmUntestedPrompt{};
            break;
        }
        case UserRequestKind::ConfirmStopSessions: {
            api::ConfirmStopSessionsPrompt confirm;
            if (const auto* payload = payload_as<updates::ConfirmStopSessionsPrompt>(request)) {
                for (const updates::LiveActivity& live : payload->live) {
                    if (!live.session) continue;
                    if (Result<sessions::SessionInfo> info = deps.sessions.get(*live.session))
                        confirm.sessions.push_back(convert::session_summary(*info));
                }
            }
            prompt.confirm_stop_sessions = std::move(confirm);
            break;
        }
    }
    return out;
}

Result<std::any> answer_for(const UserRequest& request, const api::RequestAnswer& answer) {
    const auto decision = [&]() -> std::optional<api::Decision> { return answer.decision; }();
    switch (request.kind) {
        case UserRequestKind::NeedsSecret:
            if (answer.secret_provided && *answer.secret_provided) return std::any(secrets::SecretProvided{});
            break;
        case UserRequestKind::ConfirmJoin:
            if (decision) return std::any(browser::ConfirmJoinAnswer{decision->accept});
            break;
        case UserRequestKind::NeedsJoinPassword:
            if (answer.secret_provided && *answer.secret_provided) return std::any(browser::JoinPasswordProvided{});
            break;
        case UserRequestKind::AutoServerConsent:
        case UserRequestKind::AgentRequiresApproval:
            if (decision) return std::any(decision->accept);
            break;
        case UserRequestKind::ConfirmUnencryptedUpstream:
            if (!decision) break;
            if (payload_as<front::UnencryptedUpstreamPrompt>(request) != nullptr)
                return std::any(front::UnencryptedUpstreamAnswer{decision->accept, decision->remember});
            return std::any(backend::UnencryptedUpstreamAnswer{decision->accept});
        case UserRequestKind::AccountRenameConflict:
            if (answer.rename == api::RenameConflict::KeepExisting)
                return std::any(backend::AccountRenameConflictAnswer{backend::RenameConflictChoice::KeepExisting});
            if (answer.rename == api::RenameConflict::Replace)
                return std::any(backend::AccountRenameConflictAnswer{backend::RenameConflictChoice::Replace});
            break;
        case UserRequestKind::RosettaInstall:
            if (decision)
                return std::any(decision->accept ? compat::RosettaInstallAnswer::Installed : compat::RosettaInstallAnswer::Declined);
            break;
        case UserRequestKind::ChooseVersion:
            if (answer.version) {
                Result<GameVersion> version = convert::game_version(*answer.version, "answer.version");
                if (!version) return std::unexpected(std::move(version.error()));
                return std::any(builds::UserVersion{*version, std::nullopt});
            }
            break;
        case UserRequestKind::ConfirmUntested:
            if (decision) return std::any(decision->accept);
            break;
        case UserRequestKind::ConfirmStopSessions:
            if (decision) return std::any(updates::ConfirmStopSessionsAnswer{decision->accept});
            break;
    }
    return std::unexpected(mismatch(request));
}

}  // namespace rb::engine::requests
