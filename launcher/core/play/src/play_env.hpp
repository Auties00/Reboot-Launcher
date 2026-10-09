#pragma once

#include <optional>
#include <string>
#include <vector>

#include "reboot/backend/backend_lease.hpp"
#include "reboot/backend/backend_session_config.hpp"
#include "reboot/backend/backend_state.hpp"
#include "reboot/backend/backend_upstream.hpp"
#include "reboot/backend/launch_credential.hpp"
#include "reboot/backend/remote_login.hpp"
#include "reboot/browser/game_server_target.hpp"
#include "reboot/browser/join_outcome.hpp"
#include "reboot/browser/join_service.hpp"
#include "reboot/browser/join_target.hpp"
#include "reboot/builds/build_layout.hpp"
#include "reboot/builds/installed_build.hpp"
#include "reboot/catalog/build_flags.hpp"
#include "reboot/compat/prepared_prefix.hpp"
#include "reboot/compat/prepared_runtime.hpp"
#include "reboot/compat/runner_profile.hpp"
#include "reboot/compat/wine_session_host.hpp"
#include "reboot/components/integrity_hold.hpp"
#include "reboot/components/payload_set.hpp"
#include "reboot/components/pinned_payload.hpp"
#include "reboot/components/progress_sink.hpp"
#include "reboot/foundation/cancel.hpp"
#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/function.hpp"
#include "reboot/foundation/operation.hpp"
#include "reboot/foundation/secret.hpp"
#include "reboot/foundation/types.hpp"
#include "reboot/front/front_route.hpp"
#include "reboot/front/legacy_fixed_listeners.hpp"
#include "reboot/front/session_key.hpp"
#include "reboot/host/host_event.hpp"
#include "reboot/host/host_start_request.hpp"
#include "reboot/identity/account_record.hpp"
#include "reboot/identity/login_target.hpp"
#include "reboot/injection/pinned_dll.hpp"
#include "reboot/play/preflight.hpp"
#include "reboot/ports/runner.hpp"
#include "reboot/ports/session_host.hpp"
#include "reboot/secrets/secret_service.hpp"
#include "reboot/secrets/secret_target.hpp"
#include "reboot/storage/settings_snapshot.hpp"
#include "reboot/support/host_inputs.hpp"
#include "reboot/support/support_policy.hpp"

namespace rb::play {

// The runner a play session would use now.
struct RunnerChoice {
    ports::RunnerKind kind = ports::RunnerKind::Native;
    // Wine runners only.
    std::optional<compat::RunnerProfile> profile;

    [[nodiscard]] RunnerMultiplier multiplier() const noexcept {
        return profile ? profile->multiplier() : RunnerMultiplier::Native;
    }
};

// A custom auth DLL that passed the checks, held since its digest was taken.
struct CustomAuth {
    injection::PinnedDll dll;
    components::IntegrityHold hold;
};

// Every service a play session reaches besides the registry, the game channel and the match
// targets, behind one seam so the session's state machine runs on fakes. Strand-only; each `done`
// runs later on the strand, exactly once.
class PlayEnv {
public:
    virtual ~PlayEnv() = default;

    [[nodiscard]] virtual storage::SettingsSnapshot settings() const = 0;
    [[nodiscard]] virtual std::optional<BuildId> selected_build() const = 0;
    [[nodiscard]] virtual Result<builds::InstalledBuild> build(BuildId id) const = 0;
    [[nodiscard]] virtual catalog::BuildFlags flags_for(const GameVersion& version) const = 0;
    // Native, or the best Wine runner whose runtimes the manifest provides.
    [[nodiscard]] virtual Result<RunnerChoice> runner() const = 0;
    [[nodiscard]] virtual std::optional<browser::JoinTarget> join_target() const = 0;
    [[nodiscard]] virtual const support::SupportPolicy& support() const = 0;
    // The binary the linked auto-server would run, once described.
    [[nodiscard]] virtual std::optional<support::HostInputs> auto_server_inputs() const = 0;
    [[nodiscard]] virtual const backend::BackendState& backend_state() const = 0;
    [[nodiscard]] virtual identity::AccountRecord client_account() const = 0;
    [[nodiscard]] virtual identity::LoginTarget login_target(const storage::BackendTarget& backend,
                                                             identity::UpstreamFlavor flavor, bool custom_auth_dll) const = 0;
    [[nodiscard]] virtual bool secret_present(const secrets::SecretTarget& target) const = 0;

    virtual void resolve_layout(BuildId id, CancelToken token, UniqueFunction<void(Result<builds::BuildLayout>)> done) = 0;
    virtual void acquire_payload(SessionId session, CancelToken token, components::ProgressSink progress,
                                 UniqueFunction<void(Result<components::PinnedPayload>)> done) = 0;
    // DllPathValidator's checks, then an IntegrityHold whose digest the inject entry carries.
    virtual void verify_custom_auth(NativePath path, CancelToken token, UniqueFunction<void(Result<CustomAuth>)> done) = 0;
    virtual void hold(const components::PayloadSet& payload, components::PayloadRole role, CancelToken token,
                      components::ProgressSink progress, UniqueFunction<void(Result<components::IntegrityHold>)> done) = 0;
    virtual void prepare_runtime(SessionId session, const compat::RunnerProfile& profile, OperationBase& op,
                                 components::ProgressSink progress,
                                 UniqueFunction<void(Result<compat::PreparedRuntime>)> done) = 0;
    // PrefixManager::prepare to the runtime, with the VC++ source pinned to the session.
    virtual void prepare_prefix(SessionId session, const compat::PreparedRuntime& runtime, std::vector<NativePath> game_dlls,
                                CancelToken token, components::ProgressSink progress,
                                UniqueFunction<void(Result<compat::PreparedPrefix>)> done) = 0;

    virtual Result<void> reconfigure_backend(const backend::BackendConfig& config) = 0;
    virtual Result<backend::BackendLease> acquire_backend(SessionId session) = 0;
    virtual void ensure_ready(const backend::BackendLease& lease, CancelToken token,
                              UniqueFunction<void(Result<backend::BackendUpstream>)> done) = 0;
    virtual void configure_session(const backend::BackendLease& lease, backend::BackendSessionConfig config,
                                   UniqueFunction<void(Result<void>)> done) = 0;
    virtual void mint_credential(const backend::BackendLease& lease, backend::LaunchCredentialRequest request,
                                 UniqueFunction<void(Result<backend::LaunchCredential>)> done) = 0;
    virtual void remote_login(backend::RemoteLoginRequest request, CancelToken token,
                              UniqueFunction<void(Result<backend::RemoteLoginResult>)> done) = 0;
    [[nodiscard]] virtual Result<SecretBytes> provide_secret(const secrets::SecretTarget& target) const = 0;
    virtual std::optional<RequestId> require_secret(const secrets::SecretTarget& target, secrets::SecretWait wait,
                                                    CancelToken token, UniqueFunction<void(Result<SecretBytes>)> done) = 0;

    virtual Result<void> join(browser::JoinRequest request, OperationBase& op, std::optional<SessionId> session,
                              UniqueFunction<void(Result<browser::JoinOutcome>)> done) = 0;
    virtual Result<void> resolve_address(const HostPort& address, OperationBase& op,
                                         UniqueFunction<void(Result<browser::CheckedAddress>)> done) = 0;
    virtual Result<OpHandle> start_auto_server(host::HostStartRequest request) = 0;
    [[nodiscard]] virtual Result<host::HostListening> listening(SessionId server) const = 0;

    [[nodiscard]] virtual Result<std::string> origin(const front::SessionKey& key) const = 0;
    virtual Result<void> add_route(front::FrontRoute route) = 0;
    virtual void remove_route(SessionId session) = 0;
    virtual Result<void> open_fixed(front::LegacyFixedRequest request, CancelToken token,
                                    UniqueFunction<void(Result<void>)> done) = 0;
    virtual void close_fixed(SessionId session) = 0;

    // Wine runners only: WineSessionHost::stage and discard.
    virtual Result<void> stage_wine(SessionId session, compat::WineSessionSetup setup) = 0;
    virtual void discard_wine(SessionId session) = 0;
    [[nodiscard]] virtual ports::ISessionHost& session_host() = 0;
    // The pinned payload and runtime completed a session.
    virtual void mark_good(const Preflight& preflight) = 0;
};

}  // namespace rb::play
