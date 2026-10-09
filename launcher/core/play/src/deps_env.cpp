#include "deps_env.hpp"

#include <utility>

#include "reboot/backend/backend_service.hpp"
#include "reboot/browser/game_server_target.hpp"
#include "reboot/browser/join_service.hpp"
#include "reboot/builds/library.hpp"
#include "reboot/catalog/catalog_service.hpp"
#include "reboot/compat/prefix_manager.hpp"
#include "reboot/compat/runtime_service.hpp"
#include "reboot/components/component_store.hpp"
#include "reboot/foundation/executor.hpp"
#include "reboot/front/legacy_fixed_listeners.hpp"
#include "reboot/front/session_front.hpp"
#include "reboot/gameserver/game_server_binary.hpp"
#include "reboot/host/host_service.hpp"
#include "reboot/identity/identity_service.hpp"
#include "reboot/injection/dll_path_validator.hpp"
#include "reboot/storage/settings.hpp"

namespace reboot::play {

storage::SettingsSnapshot DepsEnv::settings() const { return deps_.settings.snapshot(); }

std::optional<BuildId> DepsEnv::selected_build() const { return deps_.library.selected(support::SupportRole::Play); }

Result<builds::InstalledBuild> DepsEnv::build(BuildId id) const { return deps_.library.get(id); }

catalog::BuildFlags DepsEnv::flags_for(const GameVersion& version) const {
    return deps_.catalog.current().flags_for(version);
}

Result<RunnerChoice> DepsEnv::runner() const {
    const auto* wine = std::get_if<WineRunner>(&deps_.runner);
    if (wine == nullptr) return RunnerChoice{};
    std::optional<Diagnostic> last;
    for (const compat::RunnerKind kind : wine->runtime.supported()) {
        Result<compat::RunnerProfile> profile = wine->runtime.profile(kind);
        if (profile) return RunnerChoice{kind, std::move(*profile)};
        last = std::move(profile.error());
    }
    if (last) return std::unexpected(std::move(*last));
    // No runner at all: profile() names the problem for the plain Wine runner.
    Result<compat::RunnerProfile> fallback = wine->runtime.profile(ports::RunnerKind::Wine);
    if (!fallback) return std::unexpected(std::move(fallback.error()));
    return RunnerChoice{ports::RunnerKind::Wine, std::move(*fallback)};
}

std::optional<browser::JoinTarget> DepsEnv::join_target() const { return deps_.addresses.current(); }

const support::SupportPolicy& DepsEnv::support() const { return deps_.support; }

std::optional<support::HostInputs> DepsEnv::auto_server_inputs() const {
    const gameserver::DescribedBinary* binary = deps_.game_server.current();
    if (binary == nullptr) return std::nullopt;
    Result<support::HostInputs> inputs = support::host_inputs_from(binary->sha256, binary->description);
    if (!inputs) return std::nullopt;
    return std::move(*inputs);
}

const backend::BackendState& DepsEnv::backend_state() const { return deps_.backend.state(); }

identity::AccountRecord DepsEnv::client_account() const { return deps_.identity.record(identity::AccountRole::Client); }

identity::LoginTarget DepsEnv::login_target(const storage::BackendTarget& backend, identity::UpstreamFlavor flavor,
                                            bool custom_auth_dll) const {
    return deps_.identity.login_target(backend, flavor, custom_auth_dll);
}

bool DepsEnv::secret_present(const secrets::SecretTarget& target) const {
    const Result<secrets::SecretState> state = deps_.secrets.state(target);
    return state && state->present();
}

void DepsEnv::resolve_layout(BuildId id, CancelToken token, UniqueFunction<void(Result<builds::BuildLayout>)> done) {
    deps_.library.resolve_layout(id, std::move(token), std::move(done));
}

void DepsEnv::acquire_payload(SessionId session, CancelToken token, components::ProgressSink progress,
                              UniqueFunction<void(Result<components::PinnedPayload>)> done) {
    deps_.components.acquire_payload(session, std::move(token), std::move(progress), std::move(done));
}

void DepsEnv::verify_custom_auth(NativePath path, CancelToken token, UniqueFunction<void(Result<CustomAuth>)> done) {
    deps_.workers.submit<CustomAuth>(
        [&fs = deps_.fs, path = std::move(path)](CancelToken) -> Result<CustomAuth> {
            Result<injection::PinnedDll> validated = injection::DllPathValidator(fs).validate(path);
            if (!validated) return std::unexpected(std::move(validated.error()));
            auto hold = components::IntegrityHold::acquire(fs, validated->path);
            if (!hold) return std::unexpected(components::to_diagnostic(hold.error()));
            // The inject digest is of the held bytes, not of the validator's earlier read.
            injection::PinnedDll dll{validated->path, hold->sha256()};
            return CustomAuth{std::move(dll), std::move(*hold)};
        },
        std::move(token), deps_.strand, std::move(done));
}

void DepsEnv::hold(const components::PayloadSet& payload, components::PayloadRole role, CancelToken token,
                   components::ProgressSink progress, UniqueFunction<void(Result<components::IntegrityHold>)> done) {
    deps_.components.hold(payload, role, std::move(token), std::move(progress), std::move(done));
}

void DepsEnv::prepare_runtime(SessionId session, const compat::RunnerProfile& profile, OperationBase& op,
                              components::ProgressSink progress,
                              UniqueFunction<void(Result<compat::PreparedRuntime>)> done) {
    auto* wine = std::get_if<WineRunner>(&deps_.runner);
    if (wine == nullptr) {
        deps_.strand.post([done = std::move(done)]() mutable { done(std::unexpected(internal_bug("play.prepare_runtime"))); });
        return;
    }
    wine->runtime.prepare(session, profile, op, std::move(progress), std::move(done));
}

void DepsEnv::prepare_prefix(SessionId session, const compat::PreparedRuntime& runtime, std::vector<NativePath> game_dlls,
                             CancelToken token, components::ProgressSink progress,
                             UniqueFunction<void(Result<compat::PreparedPrefix>)> done) {
    auto* wine = std::get_if<WineRunner>(&deps_.runner);
    if (wine == nullptr) {
        deps_.strand.post([done = std::move(done)]() mutable { done(std::unexpected(internal_bug("play.prepare_prefix"))); });
        return;
    }
    compat::PrefixRequest request;
    request.layout = runtime.layout;
    request.runtime = runtime.profile.runtime;
    request.runtime_version = runtime.wine.runtime.ref.version;
    request.env = daemon_env_;
    request.game_dlls = std::move(game_dlls);
    request.vc_redist = wine->runtime.vc_redist_source(session, token);
    wine->prefixes.prepare(runtime.lease, std::move(request), std::move(token), std::move(progress), std::move(done));
}

Result<void> DepsEnv::reconfigure_backend(const backend::BackendConfig& config) {
    Result<backend::ReconfigureTiming> applied = deps_.backend.reconfigure(config, backend::RunningPolicy::Refuse);
    if (!applied) return std::unexpected(std::move(applied.error()));
    return {};
}

Result<backend::BackendLease> DepsEnv::acquire_backend(SessionId session) { return deps_.backend.acquire(session); }

void DepsEnv::ensure_ready(const backend::BackendLease& lease, CancelToken token,
                           UniqueFunction<void(Result<backend::BackendUpstream>)> done) {
    deps_.backend.ensure_ready(lease, std::move(token), std::move(done));
}

void DepsEnv::configure_session(const backend::BackendLease& lease, backend::BackendSessionConfig config,
                                UniqueFunction<void(Result<void>)> done) {
    deps_.backend.configure_session(lease, std::move(config), std::move(done));
}

void DepsEnv::mint_credential(const backend::BackendLease& lease, backend::LaunchCredentialRequest request,
                              UniqueFunction<void(Result<backend::LaunchCredential>)> done) {
    deps_.backend.mint_launch_credential(lease, std::move(request), std::move(done));
}

void DepsEnv::remote_login(backend::RemoteLoginRequest request, CancelToken token,
                           UniqueFunction<void(Result<backend::RemoteLoginResult>)> done) {
    deps_.remote_login.login(std::move(request), std::move(token), std::move(done));
}

Result<SecretBytes> DepsEnv::provide_secret(const secrets::SecretTarget& target) const {
    return deps_.secrets.provide(target);
}

std::optional<RequestId> DepsEnv::require_secret(const secrets::SecretTarget& target, secrets::SecretWait wait,
                                                 CancelToken token, UniqueFunction<void(Result<SecretBytes>)> done) {
    return deps_.secrets.require(target, wait, std::move(token), std::move(done));
}

Result<void> DepsEnv::join(browser::JoinRequest request, OperationBase& op, std::optional<SessionId> session,
                           UniqueFunction<void(Result<browser::JoinOutcome>)> done) {
    return deps_.join.join(std::move(request), op, session, std::move(done));
}

Result<void> DepsEnv::resolve_address(const HostPort& address, OperationBase& op,
                                      UniqueFunction<void(Result<browser::CheckedAddress>)> done) {
    return deps_.addresses.resolve(address, op, std::move(done));
}

Result<OpHandle> DepsEnv::start_auto_server(host::HostStartRequest request) { return deps_.hosts.start(std::move(request)); }

Result<host::HostListening> DepsEnv::listening(SessionId server) const { return deps_.hosts.listening(server); }

Result<std::string> DepsEnv::origin(const front::SessionKey& key) const { return deps_.front.origin(key); }

Result<void> DepsEnv::add_route(front::FrontRoute route) { return deps_.front.add_route(std::move(route)); }

void DepsEnv::remove_route(SessionId session) { deps_.front.remove_route(session); }

Result<void> DepsEnv::open_fixed(front::LegacyFixedRequest request, CancelToken token,
                                 UniqueFunction<void(Result<void>)> done) {
    return deps_.legacy_listeners.open(std::move(request), std::move(token), std::move(done));
}

void DepsEnv::close_fixed(SessionId session) { deps_.legacy_listeners.close(session); }

Result<void> DepsEnv::stage_wine(SessionId session, compat::WineSessionSetup setup) {
    auto* wine = std::get_if<WineRunner>(&deps_.runner);
    if (wine == nullptr) return std::unexpected(internal_bug("play.stage_wine"));
    return wine->host.stage(session, std::move(setup));
}

void DepsEnv::discard_wine(SessionId session) {
    if (auto* wine = std::get_if<WineRunner>(&deps_.runner)) wine->host.discard(session);
}

ports::ISessionHost& DepsEnv::session_host() {
    if (auto* native = std::get_if<NativeRunner>(&deps_.runner)) return native->host;
    return std::get<WineRunner>(deps_.runner).host;
}

void DepsEnv::mark_good(const Preflight& preflight) {
    deps_.components.mark_good(preflight.payload.pin);
    if (!preflight.wine) return;
    if (auto* wine = std::get_if<WineRunner>(&deps_.runner)) wine->runtime.mark_good(preflight.wine->runtime);
}

}  // namespace reboot::play
