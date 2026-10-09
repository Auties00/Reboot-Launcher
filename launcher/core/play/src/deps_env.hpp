#pragma once

#include <utility>

#include "play_env.hpp"
#include "reboot/play/play_service.hpp"
#include "reboot/ports/process.hpp"

namespace rb::play {

// PlayEnv over the engine's services.
class DepsEnv final : public PlayEnv {
public:
    // `deps` must outlive this; `daemon_env` is EnvBuilder's base for the prefix commands.
    DepsEnv(PlayServiceDeps& deps, ports::EnvBlock daemon_env) : deps_(deps), daemon_env_(std::move(daemon_env)) {}

    [[nodiscard]] storage::SettingsSnapshot settings() const override;
    [[nodiscard]] std::optional<BuildId> selected_build() const override;
    [[nodiscard]] Result<builds::InstalledBuild> build(BuildId id) const override;
    [[nodiscard]] catalog::BuildFlags flags_for(const GameVersion& version) const override;
    [[nodiscard]] Result<RunnerChoice> runner() const override;
    [[nodiscard]] std::optional<browser::JoinTarget> join_target() const override;
    [[nodiscard]] const support::SupportPolicy& support() const override;
    [[nodiscard]] std::optional<support::HostInputs> auto_server_inputs() const override;
    [[nodiscard]] const backend::BackendState& backend_state() const override;
    [[nodiscard]] identity::AccountRecord client_account() const override;
    [[nodiscard]] identity::LoginTarget login_target(const storage::BackendTarget& backend, identity::UpstreamFlavor flavor,
                                                     bool custom_auth_dll) const override;
    [[nodiscard]] bool secret_present(const secrets::SecretTarget& target) const override;

    void resolve_layout(BuildId id, CancelToken token, UniqueFunction<void(Result<builds::BuildLayout>)> done) override;
    void acquire_payload(SessionId session, CancelToken token, components::ProgressSink progress,
                         UniqueFunction<void(Result<components::PinnedPayload>)> done) override;
    void verify_custom_auth(NativePath path, CancelToken token, UniqueFunction<void(Result<CustomAuth>)> done) override;
    void hold(const components::PayloadSet& payload, components::PayloadRole role, CancelToken token,
              components::ProgressSink progress, UniqueFunction<void(Result<components::IntegrityHold>)> done) override;
    void prepare_runtime(SessionId session, const compat::RunnerProfile& profile, OperationBase& op,
                         components::ProgressSink progress,
                         UniqueFunction<void(Result<compat::PreparedRuntime>)> done) override;
    void prepare_prefix(SessionId session, const compat::PreparedRuntime& runtime, std::vector<NativePath> game_dlls,
                        CancelToken token, components::ProgressSink progress,
                        UniqueFunction<void(Result<compat::PreparedPrefix>)> done) override;

    Result<void> reconfigure_backend(const backend::BackendConfig& config) override;
    Result<backend::BackendLease> acquire_backend(SessionId session) override;
    void ensure_ready(const backend::BackendLease& lease, CancelToken token,
                      UniqueFunction<void(Result<backend::BackendUpstream>)> done) override;
    void configure_session(const backend::BackendLease& lease, backend::BackendSessionConfig config,
                           UniqueFunction<void(Result<void>)> done) override;
    void mint_credential(const backend::BackendLease& lease, backend::LaunchCredentialRequest request,
                         UniqueFunction<void(Result<backend::LaunchCredential>)> done) override;
    void remote_login(backend::RemoteLoginRequest request, CancelToken token,
                      UniqueFunction<void(Result<backend::RemoteLoginResult>)> done) override;
    [[nodiscard]] Result<SecretBytes> provide_secret(const secrets::SecretTarget& target) const override;
    std::optional<RequestId> require_secret(const secrets::SecretTarget& target, secrets::SecretWait wait, CancelToken token,
                                            UniqueFunction<void(Result<SecretBytes>)> done) override;

    Result<void> join(browser::JoinRequest request, OperationBase& op, std::optional<SessionId> session,
                      UniqueFunction<void(Result<browser::JoinOutcome>)> done) override;
    Result<void> resolve_address(const HostPort& address, OperationBase& op,
                                 UniqueFunction<void(Result<browser::CheckedAddress>)> done) override;
    Result<OpHandle> start_auto_server(host::HostStartRequest request) override;
    [[nodiscard]] Result<host::HostListening> listening(SessionId server) const override;

    [[nodiscard]] Result<std::string> origin(const front::SessionKey& key) const override;
    Result<void> add_route(front::FrontRoute route) override;
    void remove_route(SessionId session) override;
    Result<void> open_fixed(front::LegacyFixedRequest request, CancelToken token,
                            UniqueFunction<void(Result<void>)> done) override;
    void close_fixed(SessionId session) override;

    Result<void> stage_wine(SessionId session, compat::WineSessionSetup setup) override;
    void discard_wine(SessionId session) override;
    [[nodiscard]] ports::ISessionHost& session_host() override;
    void mark_good(const Preflight& preflight) override;

private:
    PlayServiceDeps& deps_;
    ports::EnvBlock daemon_env_;
};

}  // namespace rb::play
