#pragma once

#include <any>
#include <chrono>
#include <deque>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <boost/asio/io_context.hpp>
#include <catch2/catch_test_macros.hpp>

#include "play_core.hpp"
#include "play_env.hpp"
#include "reboot/foundation/events.hpp"
#include "reboot/foundation/executor.hpp"
#include "reboot/foundation/log.hpp"
#include "reboot/foundation/operation.hpp"
#include "reboot/foundation/user_request.hpp"
#include "reboot/game_channel/game_channel_listener.hpp"
#include "reboot/game_channel/token_registry.hpp"
#include "reboot/play/match_targets.hpp"
#include "reboot/process/env_builder.hpp"
#include "reboot/sessions/session_event.hpp"
#include "reboot/sessions/session_registry.hpp"
#include "reboot/testing/deterministic_runtime.hpp"
#include "reboot/testing/event_recorder.hpp"
#include "reboot/testing/fake_client_dll.hpp"
#include "reboot/testing/fake_platform_paths.hpp"
#include "reboot/testing/fake_random.hpp"
#include "reboot/testing/fake_session_control.hpp"
#include "reboot/testing/fake_session_host.hpp"
#include "reboot/testing/fake_system_info.hpp"
#include "reboot/testing/in_memory_file_system.hpp"
#include "reboot/testing/memory_stream_pair.hpp"

namespace reboot::play::test {

namespace gc = contracts::game_client;

inline const GameVersion kVersion{12, 41, std::nullopt};
inline const Changelist kChangelist{12345};

[[nodiscard]] inline NativePath root() { return testing::default_fake_root(); }
[[nodiscard]] inline NativePath build_root() { return root() / "builds" / "12.41"; }
[[nodiscard]] inline NativePath client_dll() { return root() / "components" / "payload" / "rb_client.dll"; }
[[nodiscard]] inline NativePath winhost_exe() { return root() / "components" / "payload" / "reboot-winhost.exe"; }
[[nodiscard]] inline NativePath custom_dll() { return root() / "dlls" / "auth.dll"; }

[[nodiscard]] inline Uuid uuid_of(u8 tag) {
    Uuid uuid{};
    uuid.bytes[0] = tag;
    uuid.bytes[15] = tag;
    return uuid;
}

[[nodiscard]] inline std::string arg_text(const Diagnostic& diag, std::string_view name) {
    const Arg* arg = diag.find_arg(name);
    if (arg == nullptr) return {};
    if (const auto* text = std::get_if<std::string>(arg)) return *text;
    return {};
}

// One asynchronous step of the fake environment: answered right away, through the strand, unless held.
template <class T>
class Step {
public:
    using Done = UniqueFunction<void(Result<T>)>;

    explicit Step(UniqueFunction<Result<T>()> make) : answer(std::move(make)) {}

    void call(Executor& strand, Done done) {
        ++calls;
        if (hold) {
            pending.push_back(std::move(done));
            return;
        }
        Result<T> result = answer();
        strand.post([result = std::move(result), done = std::move(done)]() mutable { done(std::move(result)); });
    }

    // Answers the oldest held call.
    void release(Result<T> result) {
        REQUIRE_FALSE(pending.empty());
        Done done = std::move(pending.front());
        pending.pop_front();
        done(std::move(result));
    }
    void release() { release(answer()); }

    UniqueFunction<Result<T>()> answer;
    bool hold = false;
    int calls = 0;
    std::deque<Done> pending;
};

[[nodiscard]] inline Diagnostic failure(std::string_view id) {
    Diagnostic diag;
    diag.domain = domain_from_id(id);
    diag.id = std::string(id);
    return diag;
}

// PlayEnv on plain values, with each service step answered by a Step the test can hold.
class FakePlayEnv final : public PlayEnv {
public:
    struct Route {
        SessionId session;
        front::SessionKey key;
        bool embedded = true;
        std::optional<front::UpstreamOrigin> upstream;
        bool plain_http = false;
        bool tickets = false;
    };

    struct Configured {
        std::string session_key;
        std::string account_id;
        std::string origin;
        std::string console_key;
        GameVersion version;
        Changelist changelist;
    };

    struct LoginCall {
        backend::BackendInfo upstream;
        std::string login;
        bool want_exchange_code = false;
        secrets::SecretWait wait;
    };

    struct Staged {
        SessionId session;
        compat::RunnerKind kind{};
        NativePath winhost;
        NativePath prefix;
        NativePath log_dir;
        ports::EnvBlock env;
    };

    FakePlayEnv(Executor& strand, OpRegistry& ops, testing::InMemoryFileSystem& fs, ports::ISessionHost& host)
        : strand_(strand), ops_(ops), fs_(fs), host_(host) {}

    [[nodiscard]] storage::SettingsSnapshot settings() const override { return settings_value; }
    [[nodiscard]] std::optional<BuildId> selected_build() const override { return selected; }
    [[nodiscard]] Result<builds::InstalledBuild> build(BuildId id) const override {
        for (const builds::InstalledBuild& build : builds)
            if (build.id == id) return build;
        return std::unexpected(failure("builds.not_found"));
    }
    [[nodiscard]] catalog::BuildFlags flags_for(const GameVersion&) const override { return flags; }
    [[nodiscard]] Result<RunnerChoice> runner() const override {
        if (runner_error) return std::unexpected(*runner_error);
        return runner_value;
    }
    [[nodiscard]] std::optional<browser::JoinTarget> join_target() const override { return join_target_value; }
    [[nodiscard]] const support::SupportPolicy& support() const override { return policy; }
    [[nodiscard]] std::optional<support::HostInputs> auto_server_inputs() const override { return auto_inputs; }
    [[nodiscard]] const backend::BackendState& backend_state() const override { return backend; }
    [[nodiscard]] identity::AccountRecord client_account() const override { return account; }
    [[nodiscard]] identity::LoginTarget login_target(const storage::BackendTarget& target, identity::UpstreamFlavor flavor,
                                                     bool custom_auth_dll) const override {
        identity::LoginTarget out;
        out.backend = target.kind;
        out.flavor = flavor;
        if (target.kind != storage::BackendKind::Embedded) out.remote_login = remote_login_name;
        out.policy = credential_policy;
        out.custom_auth_dll = custom_auth_dll;
        return out;
    }
    [[nodiscard]] bool secret_present(const secrets::SecretTarget& target) const override {
        for (const secrets::SecretTarget& present : secrets_present)
            if (present == target) return true;
        return false;
    }

    void resolve_layout(BuildId, CancelToken token, UniqueFunction<void(Result<builds::BuildLayout>)> done) override {
        layout_token = std::move(token);
        layout.call(strand_, std::move(done));
    }
    void acquire_payload(SessionId, CancelToken, components::ProgressSink progress,
                         UniqueFunction<void(Result<components::PinnedPayload>)> done) override {
        if (progress) progress(Progress{.phase = "payload"});
        payload.call(strand_, std::move(done));
    }
    void verify_custom_auth(NativePath path, CancelToken, UniqueFunction<void(Result<CustomAuth>)> done) override {
        verified.push_back(std::move(path));
        custom_auth.call(strand_, std::move(done));
    }
    void hold(const components::PayloadSet&, components::PayloadRole role, CancelToken, components::ProgressSink,
              UniqueFunction<void(Result<components::IntegrityHold>)> done) override {
        held_roles.push_back(role);
        holds.call(strand_, std::move(done));
    }
    void prepare_runtime(SessionId, const compat::RunnerProfile& profile, OperationBase&, components::ProgressSink,
                         UniqueFunction<void(Result<compat::PreparedRuntime>)> done) override {
        runtime_profiles.push_back(profile);
        runtime.call(strand_, std::move(done));
    }
    void prepare_prefix(SessionId, const compat::PreparedRuntime&, std::vector<NativePath> game_dlls, CancelToken,
                        components::ProgressSink, UniqueFunction<void(Result<compat::PreparedPrefix>)> done) override {
        prefix_dlls = std::move(game_dlls);
        prefix.call(strand_, std::move(done));
    }

    Result<void> reconfigure_backend(const backend::BackendConfig& config) override {
        reconfigured.push_back(config);
        if (reconfigure_error) return std::unexpected(*reconfigure_error);
        return {};
    }
    Result<backend::BackendLease> acquire_backend(SessionId) override {
        ++acquired;
        if (acquire_error) return std::unexpected(*acquire_error);
        return backend::BackendLease{};
    }
    void ensure_ready(const backend::BackendLease&, CancelToken,
                      UniqueFunction<void(Result<backend::BackendUpstream>)> done) override {
        ready.call(strand_, std::move(done));
    }
    void configure_session(const backend::BackendLease&, backend::BackendSessionConfig config,
                           UniqueFunction<void(Result<void>)> done) override {
        configured_sessions.push_back(Configured{config.session_key.reveal(), config.account_id, config.origin.reveal(),
                                                 config.console_key.name, config.version, config.changelist});
        configured.call(strand_, std::move(done));
    }
    void mint_credential(const backend::BackendLease&, backend::LaunchCredentialRequest request,
                         UniqueFunction<void(Result<backend::LaunchCredential>)> done) override {
        mint_requests.push_back(std::move(request));
        minted.call(strand_, std::move(done));
    }
    void remote_login(backend::RemoteLoginRequest request, CancelToken,
                      UniqueFunction<void(Result<backend::RemoteLoginResult>)> done) override {
        logins.push_back(LoginCall{request.upstream, request.login, request.want_exchange_code, request.wait});
        if (login_request && request.awaiting_user) request.awaiting_user(*login_request);
        logged_in.call(strand_, std::move(done));
    }
    [[nodiscard]] Result<SecretBytes> provide_secret(const secrets::SecretTarget& target) const override {
        provided.push_back(target);
        if (provide_error) return std::unexpected(*provide_error);
        return SecretBytes(std::vector<u8>(stored_password.begin(), stored_password.end()));
    }
    std::optional<RequestId> require_secret(const secrets::SecretTarget& target, secrets::SecretWait wait, CancelToken,
                                            UniqueFunction<void(Result<SecretBytes>)> done) override {
        required_targets.push_back(target);
        required_waits.push_back(wait);
        required.call(strand_, std::move(done));
        return secret_request;
    }

    Result<void> join(browser::JoinRequest request, OperationBase&, std::optional<SessionId>,
                      UniqueFunction<void(Result<browser::JoinOutcome>)> done) override {
        join_requests.push_back(request);
        if (join_error) return std::unexpected(*join_error);
        joined.call(strand_, std::move(done));
        return {};
    }
    Result<void> resolve_address(const HostPort& address, OperationBase&,
                                 UniqueFunction<void(Result<browser::CheckedAddress>)> done) override {
        resolved_addresses.push_back(address);
        resolved.call(strand_, std::move(done));
        return {};
    }
    Result<OpHandle> start_auto_server(host::HostStartRequest request) override {
        auto_starts.push_back(request);
        if (auto_start_error) return std::unexpected(*auto_start_error);
        auto [handle, op] = ops_.create<SessionId>(OpKind::Host, DisconnectPolicy::Detached, std::nullopt);
        auto_ops.push_back(&op);
        return handle;
    }
    [[nodiscard]] Result<host::HostListening> listening(SessionId server) const override {
        const auto it = listenings.find(server);
        if (it == listenings.end()) return std::unexpected(failure("host.not_listening"));
        return it->second;
    }

    [[nodiscard]] Result<std::string> origin(const front::SessionKey& key) const override {
        if (origin_error) return std::unexpected(*origin_error);
        return "http://127.0.0.1:4000/s/" + key.to_hex() + "/";
    }
    Result<void> add_route(front::FrontRoute route) override {
        if (route_error) return std::unexpected(*route_error);
        Route record{route.session, route.key, true, std::nullopt, false, route.tickets.has_value()};
        if (const auto* configured_upstream = std::get_if<front::ConfiguredUpstream>(&route.upstream)) {
            record.embedded = false;
            record.upstream = configured_upstream->origin;
            record.plain_http = configured_upstream->plain_http_consented;
        }
        routes.push_back(std::move(record));
        return {};
    }
    void remove_route(SessionId session) override { removed_routes.push_back(session); }
    Result<void> open_fixed(front::LegacyFixedRequest request, CancelToken,
                            UniqueFunction<void(Result<void>)> done) override {
        fixed_requests.push_back(request);
        if (fixed_error) return std::unexpected(*fixed_error);
        fixed.call(strand_, std::move(done));
        return {};
    }
    void close_fixed(SessionId session) override { closed_fixed.push_back(session); }

    Result<void> stage_wine(SessionId session, compat::WineSessionSetup setup) override {
        Result<process::BuiltEnv> env = std::move(setup.env).build();
        REQUIRE(env);
        staged.push_back(Staged{session, setup.kind, setup.winhost_exe, setup.prefix, setup.log_dir, env->copy()});
        return {};
    }
    void discard_wine(SessionId session) override { discarded.push_back(session); }
    [[nodiscard]] ports::ISessionHost& session_host() override { return host_; }
    void mark_good(const Preflight&) override { ++good; }

    // What the queries answer.
    storage::SettingsSnapshot settings_value;
    std::optional<BuildId> selected;
    std::vector<builds::InstalledBuild> builds;
    catalog::BuildFlags flags;
    RunnerChoice runner_value;
    std::optional<Diagnostic> runner_error;
    std::optional<browser::JoinTarget> join_target_value;
    support::SupportPolicy policy{support::SupportInputs{}};
    std::optional<support::HostInputs> auto_inputs;
    backend::BackendState backend;
    identity::AccountRecord account;
    // BackendLogin::login of a Local or Remote backend.
    std::optional<std::string> remote_login_name;
    identity::CredentialPolicy credential_policy = identity::CredentialPolicy::Ticket;
    std::vector<secrets::SecretTarget> secrets_present;
    std::string stored_password = "stored-password";

    // The steps.
    Step<builds::BuildLayout> layout{[] {
        builds::BuildLayout out;
        out.root = build_root();
        out.shipping_exe = NativePath("FortniteGame/Binaries/Win64") / std::string(builds::kShippingExe);
        out.launcher_exe = NativePath("FortniteGame/Binaries/Win64") / std::string(builds::kLauncherExe);
        out.aftermath_dlls = {NativePath("Engine/Binaries/ThirdParty") / std::string(builds::kAftermathDll)};
        return Result<builds::BuildLayout>(std::move(out));
    }};
    Step<components::PinnedPayload> payload{[] {
        components::PinnedPayload out;
        out.set.ref = components::ComponentRef{components::ComponentKind::Payload, "payload", "1.0.0"};
        out.set.payload_abi = VersionStreams::payload_abi;
        components::StoredFile client{components::PayloadRole::ClientDll, client_dll(), {}, 4};
        client.sha256.fill(0x11);
        components::StoredFile winhost{components::PayloadRole::Winhost, winhost_exe(), {}, 4};
        winhost.sha256.fill(0x22);
        out.set.files = {client, winhost};
        return Result<components::PinnedPayload>(std::move(out));
    }};
    Step<CustomAuth> custom_auth{[this]() -> Result<CustomAuth> {
        auto hold = components::IntegrityHold::acquire(fs_, custom_dll());
        REQUIRE(hold);
        injection::PinnedDll dll{custom_dll(), hold->sha256()};
        return CustomAuth{std::move(dll), std::move(*hold)};
    }};
    Step<components::IntegrityHold> holds{[this]() -> Result<components::IntegrityHold> {
        auto hold = components::IntegrityHold::acquire(fs_, client_dll());
        REQUIRE(hold);
        return std::move(*hold);
    }};
    Step<compat::PreparedRuntime> runtime{[] {
        compat::PreparedRuntime out;
        out.profile = compat::RunnerProfile{compat::RunnerKind::Wine, compat::RuntimeId{"kron-10"}, std::nullopt, false};
        out.layout.root = root() / "runtime";
        out.layout.entry = root() / "runtime" / "bin" / "wine";
        out.wine.runtime.ref = components::ComponentRef{components::ComponentKind::Runtime, "kron-10", "10.0"};
        return Result<compat::PreparedRuntime>(std::move(out));
    }};
    Step<compat::PreparedPrefix> prefix{[] {
        compat::PreparedPrefix out;
        out.dir = root() / "prefixes" / "wine";
        out.paths = compat::PathMapper({compat::DosDevice{'z', (root() / "builds").lexically_normal()}});
        return Result<compat::PreparedPrefix>(std::move(out));
    }};
    Step<backend::BackendUpstream> ready{[] {
        backend::BackendUpstream out;
        out.origin = "http://127.0.0.1:5000";
        out.websocket = HostPort{"127.0.0.1", Port{5001}};
        out.http_port = 5000;
        return Result<backend::BackendUpstream>(std::move(out));
    }};
    Step<void> configured{[] { return Result<void>{}; }};
    Step<backend::LaunchCredential> minted{[] {
        return Result<backend::LaunchCredential>(backend::LaunchCredential{SecretString{std::string("minted-secret")}, {}});
    }};
    Step<backend::RemoteLoginResult> logged_in{[] {
        return Result<backend::RemoteLoginResult>(backend::RemoteLoginResult{SecretString{std::string("exchange-code")}});
    }};
    Step<SecretBytes> required{[] {
        const std::string text = "argv-password";
        return Result<SecretBytes>(SecretBytes(std::vector<u8>(text.begin(), text.end())));
    }};
    Step<browser::JoinOutcome> joined{[] {
        browser::JoinOutcome out;
        out.endpoint = Endpoint{IpAddress::v4(0x0A000001), Port{7777}};
        return Result<browser::JoinOutcome>(std::move(out));
    }};
    Step<browser::CheckedAddress> resolved{[] {
        browser::CheckedAddress out;
        out.endpoint = Endpoint{IpAddress::v4(0x0A000002), Port{7778}};
        out.probe = browser::ProbeVerdict::Reachable;
        return Result<browser::CheckedAddress>(std::move(out));
    }};
    Step<void> fixed{[] { return Result<void>{}; }};

    // Synchronous refusals.
    std::optional<Diagnostic> reconfigure_error;
    std::optional<Diagnostic> acquire_error;
    std::optional<Diagnostic> provide_error;
    std::optional<Diagnostic> join_error;
    std::optional<Diagnostic> auto_start_error;
    std::optional<Diagnostic> origin_error;
    std::optional<Diagnostic> route_error;
    std::optional<Diagnostic> fixed_error;
    // Raised by remote_login and require_secret, for the op's awaiting_user.
    std::optional<RequestId> login_request;
    std::optional<RequestId> secret_request;

    // What the core asked for.
    CancelToken layout_token;
    std::vector<NativePath> verified;
    std::vector<components::PayloadRole> held_roles;
    std::vector<compat::RunnerProfile> runtime_profiles;
    std::vector<NativePath> prefix_dlls;
    std::vector<backend::BackendConfig> reconfigured;
    int acquired = 0;
    std::vector<Configured> configured_sessions;
    std::vector<backend::LaunchCredentialRequest> mint_requests;
    std::vector<LoginCall> logins;
    mutable std::vector<secrets::SecretTarget> provided;
    std::vector<secrets::SecretTarget> required_targets;
    std::vector<secrets::SecretWait> required_waits;
    std::vector<browser::JoinRequest> join_requests;
    std::vector<HostPort> resolved_addresses;
    std::vector<host::HostStartRequest> auto_starts;
    std::vector<Operation<SessionId>*> auto_ops;
    std::map<SessionId, host::HostListening> listenings;
    std::vector<Route> routes;
    std::vector<SessionId> removed_routes;
    std::vector<front::LegacyFixedRequest> fixed_requests;
    std::vector<SessionId> closed_fixed;
    std::vector<Staged> staged;
    std::vector<SessionId> discarded;
    int good = 0;

private:
    Executor& strand_;
    OpRegistry& ops_;
    testing::InMemoryFileSystem& fs_;
    ports::ISessionHost& host_;
};

// PlayCore on manual time beside the real registry, game channel and match targets.
struct Harness {
    explicit Harness(PlayServiceOptions options = {}) {
        fs.write_text(client_dll(), "client dll bytes");
        fs.write_text(custom_dll(), "custom auth dll bytes");
        REQUIRE(channel.start());

        builds::InstalledBuild build;
        build.id = BuildId{uuid_of(1)};
        build.name = "Season 12";
        build.root = build_root();
        build.version = kVersion;
        build.cl = kChangelist;
        build.version_source = builds::VersionSource::User;
        env.builds.push_back(build);
        env.selected = build.id;
        env.account = identity::AccountRecord{AccountRecordId{uuid_of(2)}, identity::AccountRole::Client, "Player", "abc123", {}};
        env.policy.set_inputs(tested_inputs(ports::RunnerKind::Native, ""));
        make(std::move(options));
    }

    ~Harness() {
        core.reset();
        dlls.clear();
        channel.close();
    }

    Harness(const Harness&) = delete;
    Harness& operator=(const Harness&) = delete;

    void make(PlayServiceOptions options) {
        core = std::make_unique<PlayCore>(PlayCoreDeps{.env = env,
                                                       .sessions = registry,
                                                       .channel = channel,
                                                       .match_targets = targets,
                                                       .system = system,
                                                       .random = random,
                                                       .redactor = redactor,
                                                       .requests = rt.requests(),
                                                       .ops = rt.ops(),
                                                       .events = rt.events(),
                                                       .strand = rt.strand()},
                                          std::move(options));
    }

    // Tested on `runner` for 12.x with the current inputs.
    [[nodiscard]] static support::SupportInputs tested_inputs(ports::RunnerKind runner, std::string runtime_id) {
        support::SupportInputs inputs;
        inputs.runners = {support::RunnerPin{runner, runtime_id}};
        support::EvidenceRecord record;
        record.cell = support::SupportCellKey{support::VersionRange{GameVersion{12, 0, std::nullopt}, GameVersion{12, 99, std::nullopt}, {}},
                                              support::SupportRole::Play, runner};
        record.inputs = support::PlayCellInputs{{}, {}, std::move(runtime_id)};
        record.os = inputs.os;
        record.build = "12.41";
        record.version = kVersion;
        record.result = support::EvidenceResult::Pass;
        inputs.evidence = {record};
        return inputs;
    }

    void untested() { env.policy.set_inputs(support::SupportInputs{.runners = {support::RunnerPin{}}}); }

    [[nodiscard]] static PlayRequest request() {
        PlayRequest out;
        out.display.os_session = "1";
        return out;
    }

    OpHandle start(PlayRequest play = request()) {
        Result<OpHandle> started = core->start(std::move(play));
        CHECK(started.error_or(Diagnostic{}).id.empty());
        REQUIRE(started);
        return *started;
    }

    void run() { rt.run_until_idle(); }

    [[nodiscard]] std::optional<ErasedOutcome> outcome(OpHandle op) {
        run();
        return rt.ops().outcome(op.id());
    }

    [[nodiscard]] SessionId completed(OpHandle op) {
        const std::optional<ErasedOutcome> done = outcome(op);
        REQUIRE(done);
        if (const auto* failed = std::get_if<Failed>(&*done)) CHECK(failed->error.id.empty());
        const auto* value = std::get_if<Completed<std::any>>(&*done);
        REQUIRE(value != nullptr);
        return std::any_cast<SessionId>(value->value);
    }

    [[nodiscard]] Diagnostic failed(OpHandle op) {
        const std::optional<ErasedOutcome> done = outcome(op);
        REQUIRE(done);
        const auto* failure = std::get_if<Failed>(&*done);
        REQUIRE(failure != nullptr);
        return failure->error;
    }

    [[nodiscard]] std::optional<SessionId> session() const {
        for (const sessions::SessionInfo& info : registry.list())
            if (info.kind == sessions::SessionKind::Play) return info.id;
        return std::nullopt;
    }

    [[nodiscard]] std::optional<sessions::SessionPhase> phase(const SessionId& id) const {
        Result<sessions::SessionInfo> info = registry.get(id);
        if (!info) return std::nullopt;
        return info->phase;
    }

    [[nodiscard]] std::optional<sessions::SessionEnded> ended(const SessionId& id) {
        run();
        recorder.pump();
        for (const sessions::SessionEnded* end : recorder.payloads<sessions::SessionEnded>(EventKind::SessionEnded))
            if (end->session == id) return *end;
        return std::nullopt;
    }

    [[nodiscard]] std::vector<Diagnostic> degraded(const SessionId& id) {
        recorder.pump();
        std::vector<Diagnostic> out;
        for (const sessions::SessionDegraded* raised : recorder.payloads<sessions::SessionDegraded>(EventKind::SessionDegraded))
            if (raised->session == id) out.push_back(raised->condition);
        return out;
    }

    [[nodiscard]] std::optional<UserRequest> pending(UserRequestKind kind) {
        for (const UserRequest& request : rt.requests().pending())
            if (request.kind == kind) return request;
        return std::nullopt;
    }

    Result<void> answer(UserRequestKind kind, std::any value) {
        const std::optional<UserRequest> request = pending(kind);
        REQUIRE(request);
        Result<void> answered = rt.requests().respond(request->id, std::move(value));
        run();
        return answered;
    }

    testing::FakeSessionControl& launched() {
        run();
        testing::FakeSessionControl* control = host.last();
        REQUIRE(control != nullptr);
        return *control;
    }

    // Spawned, then Injected for every planned DLL, as the session host reports a good boot.
    void boot(testing::FakeSessionControl& control) {
        control.spawn_all(0x2000);
        for (const ports::InjectEntry& entry : control.launch().inject)
            control.emit(ports::Injected{entry.path, true, std::nullopt});
        run();
    }

    testing::FakeClientDll& connect(testing::FakeSessionControl& control, testing::FakeClientDllScript script = {}) {
        Result<testing::GameControlBootstrap> bootstrap = control.bootstrap();
        REQUIRE(bootstrap);
        auto dll = std::make_unique<testing::FakeClientDll>(rt.strand(), rt.clock(), std::move(script));
        testing::MemoryStreamPair pair = testing::make_memory_stream_pair(rt.strand(), {}, {});
        channel.adopt(std::move(pair.a));
        dll->attach(std::move(pair.b), *bootstrap);
        dlls.push_back(std::move(dll));
        run();
        return *dlls.back();
    }

    // A started session through Loaded, RedirectReady and LoggedIn.
    [[nodiscard]] SessionId running(PlayRequest play = request()) {
        const OpHandle op = start(std::move(play));
        testing::FakeSessionControl& control = launched();
        boot(control);
        connect(control);
        return completed(op);
    }

    void stop(const SessionId& id, sessions::StopReason reason = sessions::StopReason::User) {
        REQUIRE(registry.stop(id, sessions::StopRequest{.reason = reason}, nullptr));
        run();
    }

    testing::DeterministicRuntime rt;
    testing::FakeRandom random{11};
    Redactor redactor;
    testing::FakeSystemInfo system;
    testing::InMemoryFileSystem fs;
    sessions::SessionRegistry registry{rt.clock(), random, rt.strand(), rt.timers(), rt.events()};
    game_channel::TokenRegistry tokens{random, redactor};
    boost::asio::io_context io;
    game_channel::GameChannelListener channel{io, rt.strand(), rt.timers(), tokens};
    MatchTargets targets;
    testing::FakeSessionHost host{rt.strand()};
    testing::EventRecorder recorder{rt.events()};
    FakePlayEnv env{rt.strand(), rt.ops(), fs, host};
    std::vector<std::unique_ptr<testing::FakeClientDll>> dlls;
    std::unique_ptr<PlayCore> core;
};

}  // namespace reboot::play::test
