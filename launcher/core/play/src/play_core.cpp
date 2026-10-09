#include "play_core.hpp"

#include <algorithm>
#include <any>
#include <array>
#include <concepts>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include "messages.hpp"
#include "play_env.hpp"
#include "play_session_driver.hpp"
#include "reboot/backend/backend_config.hpp"
#include "reboot/backend/backend_info.hpp"
#include "reboot/backend/backend_target.hpp"
#include "reboot/backend/backend_url.hpp"
#include "reboot/contracts/ipc.hpp"
#include "reboot/foundation/events.hpp"
#include "reboot/foundation/executor.hpp"
#include "reboot/foundation/log.hpp"
#include "reboot/foundation/random.hpp"
#include "reboot/foundation/user_request.hpp"
#include "reboot/front/upstream_origin.hpp"
#include "reboot/game_channel/game_channel_listener.hpp"
#include "reboot/game_channel/legacy_output_adapter.hpp"
#include "reboot/host/host_profile.hpp"
#include "reboot/identity/login_plan.hpp"
#include "reboot/injection/inject_failed.hpp"
#include "reboot/injection/injection_plan.hpp"
#include "reboot/injection/legacy_requirements.hpp"
#include "reboot/play/launch_plan.hpp"
#include "reboot/play/match_targets.hpp"
#include "reboot/play/play_prompts.hpp"
#include "reboot/ports/os_services.hpp"
#include "reboot/process/env_builder.hpp"
#include "reboot/process/log_string.hpp"
#include "reboot/sessions/session_event.hpp"
#include "reboot/sessions/session_registry.hpp"
#include "reboot/storage/settings_values.hpp"
#include "wipe.hpp"

namespace rb::play {

namespace {

namespace gc = contracts::game_client;

constexpr std::size_t kEventBudget = std::size_t{256} * 1024;
constexpr std::chrono::milliseconds kStopGrace = default_deadline(OpKind::GracefulStop);
// The client DLL rewrites a URL whose host ends with one of these to the session origin.
constexpr std::array<std::string_view, 7> kRedirectHostSuffixes{
    "ol.epicgames.com", "ol.epicgames.net", "on.epicgames.com",   "epicgames.dev",
    "ak.epicgames.com", "superawesome.com", "game-social.epicgames.com"};
// The loopback endpoints builds and hotfixes target; a connect to any of them goes to the session too.
constexpr std::array<std::string_view, 4> kSentinels{"127.0.0.1:3551", "127.0.0.1:80", "[::1]:3551", "[::1]:80"};

// A browser server, joined through JoinService.
struct BrowserJoin {
    ServerId server;
    browser::JoinConfirmation confirmation = browser::JoinConfirmation::Ask;
};

// A typed address, resolved and probed at start.
struct AddressJoin {
    HostPort address;
};

struct AutoServer {};

using StartTarget = std::variant<BrowserJoin, AddressJoin, AutoServer>;

// What plan() reports and start() pins, worked out once from the request and the strand's state.
struct Inputs {
    PlayPlan plan;
    std::optional<builds::InstalledBuild> build;
    storage::SettingsSnapshot settings;
    catalog::BuildFlags flags;
    RunnerChoice runner;
    std::vector<GameArg> custom_args;
    std::optional<StartTarget> target;
    storage::BackendTarget stored_backend;
    backend::BackendConfig backend_config;
    support::SupportQuery query;
    identity::AccountRecord account;
};

[[nodiscard]] Diagnostic play_diag(MessageId message, ErrorKind kind = ErrorKind::Generic) {
    return make_diag(ErrorDomain::Play, message).kind(kind).build();
}

[[nodiscard]] std::optional<ports::BootStrategy> boot_of(std::optional<catalog::BootStrategy> strategy) noexcept {
    if (!strategy) return std::nullopt;
    return *strategy == catalog::BootStrategy::EarlyBirdApc ? ports::BootStrategy::EarlyBirdApc
                                                            : ports::BootStrategy::AfterResume;
}

[[nodiscard]] std::optional<ports::BootStrategy> build_boot(const catalog::BuildFlags& flags, ports::RunnerKind kind) {
    switch (kind) {
        case ports::RunnerKind::Native: return boot_of(flags.boot_inject.native);
        case ports::RunnerKind::Umu: return boot_of(flags.boot_inject.umu);
        case ports::RunnerKind::Wine: return boot_of(flags.boot_inject.wine);
        case ports::RunnerKind::MacRuntime: return boot_of(flags.boot_inject.mac_runtime);
    }
    return std::nullopt;
}

// The local part of -AUTH_LOGIN, which the backend reports as the account id.
[[nodiscard]] std::string account_of(const identity::LoginPlan& login) {
    const std::size_t at = login.auth_login.find('@');
    return at == std::string::npos ? login.auth_login : login.auth_login.substr(0, at);
}

[[nodiscard]] LogLevel dll_log_level() noexcept {
    if (Logger::enabled(LogLevel::Trace)) return LogLevel::Trace;
    if (Logger::enabled(LogLevel::Debug)) return LogLevel::Debug;
    return LogLevel::Info;
}

[[nodiscard]] CancelReason cancel_reason_for(sessions::StopReason reason) noexcept {
    switch (reason) {
        case sessions::StopReason::EngineShutdown: return CancelReason::Shutdown;
        case sessions::StopReason::LeaseEnded: return CancelReason::Disconnect;
        default: return CancelReason::User;
    }
}

[[nodiscard]] sessions::StopReason stop_reason_for(CancelReason reason) noexcept {
    switch (reason) {
        case CancelReason::Shutdown: return sessions::StopReason::EngineShutdown;
        case CancelReason::Disconnect: return sessions::StopReason::LeaseEnded;
        case CancelReason::Deadline: return sessions::StopReason::LaunchFailed;
        case CancelReason::User:
        case CancelReason::Superseded: break;
    }
    return sessions::StopReason::User;
}

[[nodiscard]] HostPort host_port_of(const Endpoint& endpoint) {
    return HostPort{endpoint.address.to_string(), endpoint.port};
}

[[nodiscard]] SecretString secret_text(const SecretBytes& bytes) {
    const std::vector<u8>& value = bytes.reveal();
    return SecretString{std::string(reinterpret_cast<const char*>(value.data()), value.size())};
}

void register_secret(Redactor& redactor, std::string_view value) {
    redactor.add_secret(std::span(reinterpret_cast<const u8*>(value.data()), value.size()));
}

}  // namespace

struct PlayCore::Impl {
    struct Launch {
        // Stable across the start, also before the registry gave the session an id.
        u64 key = 0;
        std::optional<SessionId> id;
        // Null once the op has its outcome.
        Operation<SessionId>* op = nullptr;
        CancelRegistration op_cancel;
        // Cancelled when the session stops, ending every pending step that takes a token.
        CancelSource work;
        // Owned by the registry; null once it destroyed the driver.
        PlaySessionDriver* driver = nullptr;
        bool stopping = false;

        // Pinned at start.
        DisplayContext display;
        sessions::Lease lease;
        bool backend_override = false;
        Inputs in;
        bool untested_confirmed = false;

        // Gathered on the way to the spawn.
        Preflight preflight;
        std::optional<compat::PreparedRuntime> runtime;
        std::optional<injection::PinnedDll> custom_auth;
        std::optional<identity::LoginTarget> login_target;
        // The -AUTH_PASSWORD value, wiped once the argv is built.
        std::optional<SecretString> credential;
        std::optional<front::TicketExchange> tickets;
        std::optional<front::SessionKey> session_key;
        std::string origin;
        std::optional<MatchTargetEntry> match;
        std::optional<OpId> linked_op;
        std::optional<gc::ClientDllConfig> dll_config;
        bool staged = false;

        // After the spawn.
        std::unique_ptr<game_channel::LegacyOutputAdapter> output;
        std::vector<NativePath> pending_inject;
        sessions::Incarnation incarnation;
        std::optional<sessions::SpawnedProcess> game_process;
    };

    Impl(PlayCoreDeps deps_in, PlayServiceOptions options_in) : deps(deps_in), options(std::move(options_in)) {
        register_secret(deps.redactor, kCalderaToken);
        subscription = deps.events.subscribe(
            EventFilter{.kinds = {EventKind::OpCompleted, EventKind::SessionEnded}, .session = {}, .op = {}}, kEventBudget);
        subscription->set_notify([this] { post([this] { drain_events(); }); });
    }

    ~Impl() {
        alive.cancel(CancelReason::Shutdown);
        subscription->set_notify({});
        for (const auto& launch : launches) {
            launch->work.cancel(CancelReason::Shutdown);
            if (launch->linked_op) (void)deps.ops.cancel(*launch->linked_op, CancelReason::Shutdown);
            // Cancelled through the registry, so the steps still holding the op let it go.
            if (launch->op != nullptr) {
                launch->op_cancel.reset();
                (void)deps.ops.cancel(launch->op->id(), CancelReason::Shutdown);
            }
            complete_op(*launch, Cancelled{CancelReason::Shutdown});
            if (launch->staged && launch->id) deps.env.discard_wine(*launch->id);
            if (PlaySessionDriver* driver = std::exchange(launch->driver, nullptr)) driver->detach();
        }
    }

    Impl(const Impl&) = delete;
    Impl& operator=(const Impl&) = delete;

    // Callbacks from other services and posted tasks run only while the service lives.
    template <class F>
    auto guard(F&& f) {
        return [alive_token = alive.token(), f = std::forward<F>(f)](auto&&... args) mutable {
            if (!alive_token.cancelled()) f(std::forward<decltype(args)>(args)...);
        };
    }

    template <class F>
    void post(F&& task) {
        deps.strand.post(guard(std::forward<F>(task)));
    }

    [[nodiscard]] Launch* find(u64 key) const {
        const auto it = std::ranges::find(launches, key, [](const auto& launch) { return launch->key; });
        return it == launches.end() ? nullptr : it->get();
    }

    [[nodiscard]] Launch* find(const SessionId& id) const {
        const auto it = std::ranges::find(launches, std::optional(id), [](const auto& launch) { return launch->id; });
        return it == launches.end() ? nullptr : it->get();
    }

    // Not stopping, and no cancel or deadline is waiting for its posted on_start_cancelled.
    [[nodiscard]] Launch* live(u64 key) const {
        Launch* launch = find(key);
        if (launch == nullptr || launch->stopping) return nullptr;
        if (launch->op != nullptr && launch->op->token().cancelled()) return nullptr;
        return launch;
    }

    void erase(u64 key) {
        std::erase_if(launches, [key](const auto& launch) { return launch->key == key; });
    }

    // ---- plan ----

    [[nodiscard]] bool play_session_live() const {
        for (const auto& launch : launches)
            if (!launch->stopping) return true;
        for (const sessions::SessionInfo& info : deps.sessions.list())
            if (info.kind == sessions::SessionKind::Play && sessions::is_active(info.phase)) return true;
        return false;
    }

    [[nodiscard]] std::optional<StartTarget> start_target(const PlayRequest& request) const {
        if (request.target)
            return std::visit(
                []<class T>(const T& target) -> StartTarget {
                    if constexpr (std::same_as<T, BrowserServerTarget>) return BrowserJoin{target.server};
                    else if constexpr (std::same_as<T, browser::AddressTarget>) return AddressJoin{target.address};
                    else return AutoServer{};
                },
                *request.target);
        const std::optional<browser::JoinTarget> current = deps.env.join_target();
        if (!current) return std::nullopt;
        return std::visit(
            []<class T>(const T& target) -> StartTarget {
                if constexpr (std::same_as<T, browser::ServerTarget>)
                    return BrowserJoin{target.id, browser::JoinConfirmation::AlreadyConfirmed};
                else return AddressJoin{target.address};
            },
            current->target);
    }

    // With the linked auto-server, never above the server's host cell once its binary is described.
    [[nodiscard]] support::SupportVerdict verdict_for(const support::SupportQuery& query, bool auto_server) const {
        const support::SupportPolicy& policy = deps.env.support();
        std::optional<support::HostInputs> inputs = auto_server ? deps.env.auto_server_inputs() : std::nullopt;
        if (!inputs) return policy.evaluate(query);
        support::SupportQuery with_server = query;
        with_server.server = std::move(*inputs);
        const support::AutoServerVerdict both = policy.evaluate_with_auto_server(with_server);
        support::SupportVerdict out = both.play;
        out.tier = both.tier;
        for (const support::SupportReason reason : both.host.reasons)
            if (std::ranges::find(out.reasons, reason) == out.reasons.end()) out.reasons.push_back(reason);
        std::ranges::stable_partition(out.reasons, [](support::SupportReason reason) {
            return support::reason_tier(reason) == support::SupportTier::Blocked;
        });
        return out;
    }

    [[nodiscard]] identity::UpstreamFlavor known_flavor(const backend::BackendConfig& config) const {
        if (config.target.embedded()) return identity::UpstreamFlavor::Reboot;
        const backend::BackendState& state = deps.env.backend_state();
        if (state.config == config && state.upstream) return state.upstream->flavor;
        return identity::UpstreamFlavor::Reboot;
    }

    [[nodiscard]] static std::optional<secrets::SecretTarget> password_target(const backend::BackendConfig& config) {
        const std::optional<backend::BackendUrl> url = config.target.upstream_url();
        if (!url) return std::nullopt;
        return secrets::SecretTarget{secrets::SecretKind::RemoteBackendPassword, secrets::SecretScope::backend(url->endpoint())};
    }

    [[nodiscard]] Result<Inputs> evaluate(const PlayRequest& request) const {
        Inputs in;
        PlayPlan& plan = in.plan;
        in.settings = deps.env.settings();
        const storage::SettingsValues& values = in.settings.values;

        if (const std::optional<BuildId> id = request.build ? request.build : deps.env.selected_build()) {
            Result<builds::InstalledBuild> build = deps.env.build(*id);
            if (!build) return std::unexpected(std::move(build.error()));
            plan.build = build->id;
            plan.version = build->version;
            if (!build->version_confirmed())
                plan.blockers.push_back(
                    make_diag(ErrorDomain::Play, msg::kBuildVersionUnknown).arg("build", build->name).kind(ErrorKind::InvalidInput));
            in.build = std::move(*build);
        } else {
            plan.blockers.push_back(play_diag(msg::kNoBuildSelected, ErrorKind::InvalidInput));
        }
        const bool confirmed = in.build && in.build->version_confirmed();
        if (confirmed) in.flags = deps.env.flags_for(*in.build->version);

        if (play_session_live()) plan.blockers.push_back(play_diag(msg::kSessionRunning, ErrorKind::Conflict));
        if (Result<void> display = check_display(request.display, deps.system.os_session(), options.session_match); !display)
            plan.blockers.push_back(std::move(display.error()));

        auto settings_args = parse_custom_args(values.play.custom_args);
        auto request_args = parse_custom_args(request.custom_args);
        if (!settings_args) plan.blockers.push_back(to_diagnostic(settings_args.error()));
        if (!request_args) plan.blockers.push_back(to_diagnostic(request_args.error()));
        if (settings_args && request_args) {
            in.custom_args = std::move(*settings_args);
            for (GameArg& arg : *request_args) in.custom_args.push_back(std::move(arg));
        }

        Result<RunnerChoice> runner = deps.env.runner();
        if (runner) in.runner = std::move(*runner);
        else plan.blockers.push_back(std::move(runner.error()));
        plan.runner = in.runner.kind;
        plan.net_mode = values.play.custom_auth_dll ? injection::NetMode::LegacyFixed : injection::NetMode::Isolated;

        in.stored_backend = values.backend.target;
        if (request.backend) request.backend->apply_to(in.stored_backend);
        storage::BackendSettings backend_settings = values.backend;
        backend_settings.target = in.stored_backend;
        Result<backend::BackendConfig> config = backend::BackendConfig::from_settings(backend_settings);
        if (config) in.backend_config = std::move(*config);
        else plan.blockers.push_back(std::move(config.error()));

        in.target = start_target(request);
        const bool auto_server = in.target && std::holds_alternative<AutoServer>(*in.target);

        support::SupportQuery& query = in.query;
        if (confirmed) {
            query.version = in.build->version;
            query.cl = in.build->cl;
        }
        query.role = support::SupportRole::Play;
        query.runner = in.runner.kind;
        query.imported = in.build.has_value();
        query.custom_auth_dll = plan.net_mode == injection::NetMode::LegacyFixed;
        query.embedded_backend = in.stored_backend.kind == storage::BackendKind::Embedded;
        plan.support = verdict_for(query, auto_server);
        // An unconfirmed version is already a blocker of its own.
        if (confirmed) {
            if (Result<void> allowed = support::check_not_blocked(query, plan.support); !allowed)
                plan.blockers.push_back(std::move(allowed.error()));
            else if (plan.support.tier == support::SupportTier::Untested) {
                for (const support::SupportReason reason : plan.support.reasons)
                    plan.warnings.push_back(support::to_diagnostic(reason, query));
                plan.decisions.push_back(RequiredDecision{UserRequestKind::ConfirmUntested, std::nullopt});
            }
        }

        in.account = deps.env.client_account();
        if (config) {
            const identity::LoginTarget target = deps.env.login_target(
                in.stored_backend, known_flavor(in.backend_config), plan.net_mode == injection::NetMode::LegacyFixed);
            Result<identity::LoginPlan> login = identity::plan_login(in.account, target, in.flags.auth_exchangecode);
            if (login) {
                for (const Diagnostic& warning : login->warnings) plan.warnings.push_back(warning);
                plan.login = std::move(*login);
            } else {
                plan.blockers.push_back(std::move(login.error()));
            }
        }

        if (auto_server) plan.decisions.push_back(RequiredDecision{UserRequestKind::AutoServerConsent, std::nullopt});
        if (const auto* join = in.target ? std::get_if<BrowserJoin>(&*in.target) : nullptr;
            join != nullptr && join->confirmation == browser::JoinConfirmation::Ask)
            plan.decisions.push_back(RequiredDecision{UserRequestKind::ConfirmJoin, std::nullopt});
        if (plan.login && identity::needs_stored_password(plan.login->delivery))
            if (const std::optional<secrets::SecretTarget> target = password_target(in.backend_config)) {
                if (!deps.env.secret_present(*target)) plan.decisions.push_back(RequiredDecision{UserRequestKind::NeedsSecret, *target});
            }
        if (config && unencrypted_upstream_pending(in.backend_config))
            plan.decisions.push_back(RequiredDecision{UserRequestKind::ConfirmUnencryptedUpstream, std::nullopt});
        return in;
    }

    // A plain-http remote the backend has not yet run on asks before it is probed.
    [[nodiscard]] bool unencrypted_upstream_pending(const backend::BackendConfig& config) const {
        const auto* remote = std::get_if<backend::RemoteBackend>(&config.target.value);
        if (remote == nullptr || remote->url.scheme != net::UrlScheme::Http) return false;
        const backend::BackendState& state = deps.env.backend_state();
        return !(state.phase == backend::BackendPhase::Running && state.config == config);
    }

    // ---- start ----

    Result<OpHandle> start(PlayRequest request, DisconnectPolicy policy) {
        Result<Inputs> in = evaluate(request);
        if (!in) return std::unexpected(std::move(in.error()));
        if (!in->plan.startable()) {
            Diagnostic first = std::move(in->plan.blockers.front());
            for (std::size_t i = 1; i < in->plan.blockers.size(); ++i) first.causes.push_back(std::move(in->plan.blockers[i]));
            return std::unexpected(std::move(first));
        }

        auto launch = std::make_unique<Launch>();
        launch->key = ++last_key;
        launch->display = std::move(request.display);
        launch->lease = request.lease;
        launch->backend_override = request.backend.has_value();
        launch->in = std::move(*in);
        const RunnerMultiplier multiplier = launch->in.runner.multiplier();

        auto [handle, op] = deps.ops.create<SessionId>(OpKind::Play, policy, std::nullopt, multiplier);
        launch->op = &op;
        const u64 key = launch->key;
        launches.push_back(std::move(launch));
        Launch& l = *launches.back();
        // Registered last: an op cancelled at once still finds its start.
        l.op_cancel = op.token().on_cancel([this, alive_token = alive.token(), key](CancelReason reason) {
            if (!alive_token.cancelled()) post([this, key, reason] { on_start_cancelled(key, reason); });
        });
        progress(l, "preparing");
        confirm_untested(l);
        return handle;
    }

    void progress(Launch& l, std::string_view phase) {
        if (l.op != nullptr) l.op->progress(Progress{.phase = phase});
    }

    [[nodiscard]] components::ProgressSink progress_sink(u64 key) {
        return [this, alive_token = alive.token(), key](const Progress& step) {
            if (alive_token.cancelled()) return;
            if (Launch* l = find(key); l != nullptr && l->op != nullptr) l->op->progress(step);
        };
    }

    void complete_op(Launch& l, Outcome<SessionId> outcome) {
        Operation<SessionId>* op = std::exchange(l.op, nullptr);
        if (op == nullptr) return;
        l.op_cancel.reset();
        op->complete(std::move(outcome));
    }

    // Any failure before the session runs: the op fails and the session, if open, stops with
    // LaunchFailed. Callers return right after, since a start with no session is erased here.
    void fail_launch(Launch& l, Diagnostic error) {
        REBOOT_LOG_AT(LogLevel::Warn, Play, l.id, "the launch failed: {}", error.id);
        complete_op(l, Failed{error});
        if (!l.id) return erase(l.key);
        if (l.stopping) return;
        begin_stop(l);
        (void)deps.sessions.stop(*l.id,
                                 sessions::StopRequest{.reason = sessions::StopReason::LaunchFailed,
                                                       .grace = kStopGrace,
                                                       .error = std::move(error)},
                                 nullptr);
    }

    void on_start_cancelled(u64 key, CancelReason reason) {
        Launch* l = find(key);
        if (l == nullptr || l->op == nullptr) return;
        // The registry already settled the outcome; this only ends the work's hold on the op.
        complete_op(*l, Cancelled{reason});
        if (!l->id) return erase(key);
        if (l->stopping) return;
        begin_stop(*l);
        sessions::StopRequest request{.reason = stop_reason_for(reason), .grace = kStopGrace, .error = std::nullopt};
        if (reason == CancelReason::Deadline) request.error = play_diag(msg::kLaunchTimedOut);
        (void)deps.sessions.stop(*l->id, std::move(request), nullptr);
    }

    // Not left to the driver's hook: the registry reaches the driver only once the linked server ended.
    void begin_stop(Launch& l) {
        l.stopping = true;
        l.work.cancel(CancelReason::Shutdown);
        if (l.linked_op) (void)deps.ops.cancel(*l.linked_op, CancelReason::User);
        if (l.staged) {
            deps.env.discard_wine(*l.id);
            l.staged = false;
        }
    }

    // The driver's stop hook, before it stops the game.
    void on_session_stop(u64 key, const sessions::StopRequest& request) {
        Launch* l = find(key);
        if (l == nullptr || l->stopping) return;
        begin_stop(*l);
        if (l->op == nullptr) return;
        if (request.error) return complete_op(*l, Failed{*request.error});
        // Cancelling settles the outcome and tells the steps still holding the op to let it go.
        const CancelReason reason = cancel_reason_for(request.reason);
        l->op_cancel.reset();
        (void)deps.ops.cancel(l->op->id(), reason);
        complete_op(*l, Cancelled{reason});
    }

    [[nodiscard]] PlaySessionHooks hooks(u64 key) {
        return PlaySessionHooks{
            .on_stop = [this, alive_token = alive.token(), key](const sessions::StopRequest& request) {
                if (!alive_token.cancelled()) on_session_stop(key, request);
            },
            .on_destroyed =
                [this, alive_token = alive.token(), key] {
                    if (alive_token.cancelled()) return;
                    if (Launch* l = find(key)) l->driver = nullptr;
                    post([this, key] { erase(key); });
                },
        };
    }

    // ---- questions ----

    using Step = void (Impl::*)(Launch&);

    void ask(Launch& l, UserRequestKind kind, std::any payload, MessageId declined, Step next) {
        const u64 key = l.key;
        const RequestId request = deps.requests.ask(
            kind, std::move(payload), l.op->id(), l.id,
            [this, alive_token = alive.token(), key, declined, next](const std::any& answer) -> Result<void> {
                const bool* yes = std::any_cast<bool>(&answer);
                if (yes == nullptr) return std::unexpected(play_diag(msg::kInvalidAnswer, ErrorKind::InvalidInput));
                if (!alive_token.cancelled())
                    post([this, key, declined, next, accepted = *yes] { on_answer(key, accepted, declined, next); });
                return {};
            },
            l.op->token());
        l.op->awaiting_user(request);
    }

    void on_answer(u64 key, bool accepted, MessageId declined, Step next) {
        Launch* l = live(key);
        if (l == nullptr || l->op == nullptr) return;
        if (!accepted) return fail_launch(*l, play_diag(declined, ErrorKind::Cancelled));
        progress(*l, "preparing");
        (this->*next)(*l);
    }

    void confirm_untested(Launch& l) {
        if (l.in.plan.support.tier != support::SupportTier::Untested) return confirm_auto_server(l);
        ask(l, UserRequestKind::ConfirmUntested, UntestedPlayPrompt{l.in.query, l.in.plan.support}, msg::kUntestedDeclined,
            &Impl::on_untested_confirmed);
    }

    void on_untested_confirmed(Launch& l) {
        l.untested_confirmed = true;
        confirm_auto_server(l);
    }

    void confirm_auto_server(Launch& l) {
        if (!l.in.target || !std::holds_alternative<AutoServer>(*l.in.target)) return open_session(l);
        ask(l, UserRequestKind::AutoServerConsent, AutoServerConsentPrompt{*l.in.build->version}, msg::kAutoServerDeclined,
            &Impl::open_session);
    }

    // ---- preflight ----

    void open_session(Launch& l) {
        const u64 key = l.key;
        auto driver = std::make_unique<PlaySessionDriver>(
            PlaySessionReleases{deps.match_targets, deps.env}, hooks(key));
        PlaySessionDriver* raw = driver.get();
        sessions::SessionSpec spec;
        spec.kind = sessions::SessionKind::Play;
        spec.lease = l.lease;
        spec.pinned.settings = l.in.settings;
        spec.pinned.build = l.in.build->id;
        if (l.in.runner.profile) spec.pinned.runtime_id = l.in.runner.profile->runtime.value;
        spec.label = l.in.build->name;
        spec.version = *l.in.build->version;
        spec.runner = l.in.runner.kind;
        Result<SessionId> id = deps.sessions.open(std::move(spec), std::move(driver));
        Launch* again = find(key);
        if (again == nullptr) return;
        if (!id) return fail_launch(*again, std::move(id.error()));
        again->id = *id;
        again->driver = raw;
        raw->bind(*id);
        raw->state().net_mode = again->in.plan.net_mode;
        REBOOT_LOG_AT(LogLevel::Info, Play, again->id, "starting {} ({})", again->in.build->name,
                      again->in.build->version->canonical());
        resolve_layout(*again);
    }

    void resolve_layout(Launch& l) {
        progress(l, "resolving_build");
        const u64 key = l.key;
        deps.env.resolve_layout(l.in.build->id, l.work.token(), guard([this, key](Result<builds::BuildLayout> layout) {
            Launch* found = live(key);
            if (found == nullptr) return;
            if (!layout) return fail_launch(*found, std::move(layout.error()));
            found->preflight.layout = std::move(*layout);
            acquire_payload(*found);
        }));
    }

    void acquire_payload(Launch& l) {
        progress(l, "pinning_payload");
        const u64 key = l.key;
        deps.env.acquire_payload(*l.id, l.work.token(), progress_sink(key),
                                        guard([this, key](Result<components::PinnedPayload> payload) {
                                            Launch* found = live(key);
                                            if (found == nullptr) return;
                                            if (!payload) return fail_launch(*found, std::move(payload.error()));
                                            found->preflight.payload = std::move(*payload);
                                            verify_custom_auth(*found);
                                        }));
    }

    void verify_custom_auth(Launch& l) {
        const std::optional<NativePath>& path = l.in.settings.values.play.custom_auth_dll;
        if (!path) return prepare_runtime(l);
        progress(l, "verifying_dlls");
        const u64 key = l.key;
        deps.env.verify_custom_auth(*path, l.work.token(), guard([this, key](Result<CustomAuth> verified) {
                Launch* found = live(key);
                if (found == nullptr) return;
                if (!verified) return fail_launch(*found, std::move(verified.error()));
                found->custom_auth = verified->dll;
                found->preflight.holds.push_back(std::move(verified->hold));
                prepare_runtime(*found);
            }));
    }

    [[nodiscard]] static const components::StoredFile* payload_file(const Launch& l, components::PayloadRole role) {
        return l.preflight.payload.set.find(role);
    }

    void prepare_runtime(Launch& l) {
        if (!l.in.runner.profile) return hold_client_dll(l);
        progress(l, "preparing_runtime");
        const u64 key = l.key;
        deps.env.prepare_runtime(*l.id, *l.in.runner.profile, *l.op, progress_sink(key),
                              guard([this, key](Result<compat::PreparedRuntime> runtime) {
                                  Launch* found = live(key);
                                  if (found == nullptr) return;
                                  if (!runtime) return fail_launch(*found, std::move(runtime.error()));
                                  found->runtime.emplace(std::move(*runtime));
                                  prepare_prefix(*found);
                              }));
    }

    void prepare_prefix(Launch& l) {
        const components::StoredFile* client = payload_file(l, components::PayloadRole::ClientDll);
        if (client == nullptr) return fail_launch(l, internal_bug("play.prepare_prefix: no client DLL"));
        progress(l, "preparing_prefix");
        // The DLLs loaded into the game decide whether the prefix needs the VC++ runtime.
        std::vector<NativePath> game_dlls{client->path};
        if (l.custom_auth) game_dlls.push_back(l.custom_auth->path);
        const u64 key = l.key;
        deps.env.prepare_prefix(*l.id, *l.runtime, std::move(game_dlls), l.work.token(), progress_sink(key),
                              guard([this, key](Result<compat::PreparedPrefix> prefix) {
                                  Launch* found = live(key);
                                  if (found == nullptr) return;
                                  if (!prefix) return fail_launch(*found, std::move(prefix.error()));
                                  found->preflight.wine.emplace(WinePins{std::move(*found->runtime), std::move(*prefix)});
                                  found->runtime.reset();
                                  start_backend(*found);
                              }));
    }

    // Windows only: under Wine winhost holds the files with its own share-denied handles.
    void hold_client_dll(Launch& l) {
        progress(l, "verifying_dlls");
        const u64 key = l.key;
        deps.env.hold(l.preflight.payload.set, components::PayloadRole::ClientDll, l.work.token(),
                             progress_sink(key), guard([this, key](Result<components::IntegrityHold> hold) {
                                 Launch* found = live(key);
                                 if (found == nullptr) return;
                                 if (!hold) return fail_launch(*found, std::move(hold.error()));
                                 found->preflight.holds.push_back(std::move(*hold));
                                 start_backend(*found);
                             }));
    }

    void start_backend(Launch& l) {
        progress(l, "starting_backend");
        if (l.backend_override) {
            const backend::BackendState& state = deps.env.backend_state();
            if (state.config != l.in.backend_config && state.pending_config != l.in.backend_config)
                if (Result<void> applied =
                        deps.env.reconfigure_backend(l.in.backend_config);
                    !applied)
                    return fail_launch(l, std::move(applied.error()));
        }
        Result<backend::BackendLease> lease = deps.env.acquire_backend(*l.id);
        if (!lease) return fail_launch(l, std::move(lease.error()));
        l.preflight.lease = std::move(*lease);
        const u64 key = l.key;
        deps.env.ensure_ready(l.preflight.lease, l.work.token(), guard([this, key](Result<backend::BackendUpstream> upstream) {
            Launch* found = live(key);
            if (found == nullptr) return;
            if (!upstream) return fail_launch(*found, std::move(upstream.error()));
            found->preflight.upstream = std::move(*upstream);
            plan_credential(*found);
        }));
    }

    // The login is planned again now that the upstream's flavor is known.
    void plan_credential(Launch& l) {
        const bool legacy = l.in.plan.net_mode == injection::NetMode::LegacyFixed;
        identity::LoginTarget target =
            deps.env.login_target(l.in.stored_backend, l.preflight.upstream.flavor, legacy);
        Result<identity::LoginPlan> login = identity::plan_login(l.in.account, target, l.in.flags.auth_exchangecode);
        if (!login) return fail_launch(l, std::move(login.error()));
        for (const Diagnostic& warning : login->warnings)
            REBOOT_LOG_AT(LogLevel::Warn, Play, l.id, "login warning: {}", warning.id);
        l.login_target = std::move(target);
        l.preflight.account = l.in.account;
        l.preflight.login = std::move(*login);
        progress(l, "logging_in");
        std::visit(
            [&]<class D>(const D& delivery) {
                if constexpr (std::same_as<D, identity::BackendMinted>) {
                    resolve_target(l);
                } else if constexpr (std::same_as<D, identity::RemotePasswordExchange>) {
                    remote_login(l, true);
                } else if constexpr (std::same_as<D, identity::FrontTicket>) {
                    if (delivery.redemption == identity::TicketRedemption::SwapForStoredPassword) return remote_login(l, false);
                    l.credential = new_ticket();
                    resolve_target(l);
                } else {
                    require_password(l);
                }
            },
            l.preflight.login.delivery);
    }

    [[nodiscard]] SecretString new_ticket() {
        std::string ticket = random_token_hex(deps.random, 32);
        register_secret(deps.redactor, ticket);
        return SecretString{std::move(ticket)};
    }

    [[nodiscard]] Result<secrets::SecretTarget> upstream_secret(const Launch& l) const {
        Result<backend::BackendUrl> url = backend::BackendUrl::parse(l.preflight.upstream.origin);
        if (!url) return std::unexpected(std::move(url.error()));
        return secrets::SecretTarget{secrets::SecretKind::RemoteBackendPassword, secrets::SecretScope::backend(url->endpoint())};
    }

    [[nodiscard]] UniqueFunction<void(RequestId)> awaiting_user(u64 key) {
        return [this, alive_token = alive.token(), key](RequestId request) {
            if (alive_token.cancelled()) return;
            if (Launch* l = find(key); l != nullptr && l->op != nullptr) l->op->awaiting_user(request);
        };
    }

    void remote_login(Launch& l, bool exchange_code) {
        Result<backend::BackendUrl> url = backend::BackendUrl::parse(l.preflight.upstream.origin);
        if (!url) return fail_launch(l, std::move(url.error()));
        if (!l.login_target->remote_login) return fail_launch(l, internal_bug("play.remote_login: no login"));
        backend::RemoteLoginRequest request;
        request.upstream.url = std::move(*url);
        request.upstream.flavor = l.preflight.upstream.flavor;
        request.login = *l.login_target->remote_login;
        request.want_exchange_code = exchange_code;
        request.wait = secrets::SecretWait{l.id, l.op->id(), secrets::NeedsSecretReason::Missing};
        const u64 key = l.key;
        request.awaiting_user = awaiting_user(key);
        deps.env.remote_login(std::move(request), l.work.token(),
                                guard([this, key, exchange_code](Result<backend::RemoteLoginResult> result) {
                                    Launch* found = live(key);
                                    if (found == nullptr) return;
                                    if (!result) return fail_launch(*found, std::move(result.error()));
                                    if (!exchange_code) return swap_ticket(*found);
                                    if (!result->exchange_code)
                                        return fail_launch(*found, internal_bug("play.remote_login: no exchange code"));
                                    found->credential = std::move(*result->exchange_code);
                                    resolve_target(*found);
                                }));
    }

    // The front swaps the game's ticket for the stored login the remote login just checked.
    void swap_ticket(Launch& l) {
        Result<secrets::SecretTarget> target = upstream_secret(l);
        if (!target) return fail_launch(l, std::move(target.error()));
        Result<SecretBytes> password = deps.env.provide_secret(*target);
        if (!password) return fail_launch(l, std::move(password.error()));
        SecretString ticket = new_ticket();
        l.credential = SecretString{std::string(ticket.reveal())};
        const front::TicketBinding binding = l.in.plan.net_mode == injection::NetMode::LegacyFixed
                                                 ? front::TicketBinding::Unbound
                                                 : front::TicketBinding::Bound;
        l.tickets.emplace(std::move(ticket), front::UpstreamLogin{*l.login_target->remote_login, secret_text(*password)},
                          binding);
        resolve_target(l);
    }

    void require_password(Launch& l) {
        Result<secrets::SecretTarget> target = upstream_secret(l);
        if (!target) return fail_launch(l, std::move(target.error()));
        const u64 key = l.key;
        const std::optional<RequestId> raised = deps.env.require_secret(
            *target, secrets::SecretWait{l.id, l.op->id(), secrets::NeedsSecretReason::Missing}, l.work.token(),
            guard([this, key](Result<SecretBytes> password) {
                Launch* found = live(key);
                if (found == nullptr) return;
                if (!password) return fail_launch(*found, std::move(password.error()));
                found->credential = secret_text(*password);
                resolve_target(*found);
            }));
        if (raised && l.op != nullptr) l.op->awaiting_user(*raised);
    }

    // ---- target ----

    void resolve_target(Launch& l) {
        if (!l.in.target) return configure_session(l);
        const u64 key = l.key;
        std::visit(
            [&]<class T>(const T& target) {
                if constexpr (std::same_as<T, BrowserJoin>) {
                    progress(l, "joining");
                    browser::JoinRequest request{target.server, target.confirmation, l.in.build->version};
                    Result<void> sent = deps.env.join(std::move(request), *l.op, l.id,
                                                       guard([this, key](Result<browser::JoinOutcome> joined) {
                                                           Launch* found = live(key);
                                                           if (found == nullptr) return;
                                                           if (!joined) return fail_launch(*found, std::move(joined.error()));
                                                           found->match = MatchTargetEntry{
                                                               account_of(found->preflight.login),
                                                               host_port_of(joined->endpoint), std::nullopt};
                                                           configure_session(*found);
                                                       }));
                    if (!sent) fail_launch(l, std::move(sent.error()));
                } else if constexpr (std::same_as<T, AddressJoin>) {
                    progress(l, "resolving_target");
                    Result<void> sent = deps.env.resolve_address(
                        target.address, *l.op, guard([this, key](Result<browser::CheckedAddress> checked) {
                            Launch* found = live(key);
                            if (found == nullptr) return;
                            if (!checked) return fail_launch(*found, std::move(checked.error()));
                            if (checked->warning) {
                                REBOOT_LOG_AT(LogLevel::Warn, Play, found->id, "the game server did not answer: {}",
                                              checked->warning->id);
                                (void)deps.sessions.raise_degraded(*found->id, *checked->warning);
                            }
                            found->match = MatchTargetEntry{account_of(found->preflight.login),
                                                            host_port_of(checked->endpoint), std::nullopt};
                            configure_session(*found);
                        }));
                    if (!sent) fail_launch(l, std::move(sent.error()));
                } else {
                    start_auto_server(l);
                }
            },
            *l.in.target);
    }

    void start_auto_server(Launch& l) {
        progress(l, "starting_server");
        host::HostStartRequest request;
        request.profile = host::kAutoProfileId;
        request.overrides.build = l.in.build->id;
        request.disconnect = DisconnectPolicy::Detached;
        request.linked_to = l.id;
        request.untested_confirmed = l.untested_confirmed;
        Result<OpHandle> started = deps.env.start_auto_server(std::move(request));
        if (!started) return fail_launch(l, make_diag(ErrorDomain::Play, msg::kLinkedServerFailed).cause(std::move(started.error())));
        l.linked_op = started->id();
        // The OpCompleted subscription delivers the outcome; one already settled is taken now.
        if (std::optional<ErasedOutcome> outcome = deps.ops.outcome(started->id())) on_linked_outcome(l, std::move(*outcome));
    }

    void on_linked_outcome(Launch& l, ErasedOutcome outcome) {
        if (!l.linked_op || l.stopping) return;
        l.linked_op.reset();
        auto* completed = std::get_if<Completed<std::any>>(&outcome);
        const SessionId* server = completed != nullptr ? std::any_cast<SessionId>(&completed->value) : nullptr;
        if (server == nullptr) {
            auto failure = make_diag(ErrorDomain::Play, msg::kLinkedServerFailed);
            if (auto* failed = std::get_if<Failed>(&outcome)) std::move(failure).cause(std::move(failed->error));
            return fail_launch(l, std::move(failure).build());
        }
        Result<host::HostListening> listening = deps.env.listening(*server);
        if (!listening)
            return fail_launch(l, make_diag(ErrorDomain::Play, msg::kLinkedServerFailed).cause(std::move(listening.error())));
        std::optional<Port> game;
        std::optional<Port> beacon;
        for (const gameserver::BoundSocket& bound : listening->bound) {
            if (bound.role == contracts::game_server::SocketRole::Game && !game) game = Port{bound.port};
            if (bound.role == contracts::game_server::SocketRole::Beacon && !beacon) beacon = Port{bound.port};
        }
        l.match = MatchTargetEntry{account_of(l.preflight.login),
                                   HostPort{"127.0.0.1", game.value_or(listening->block.first)}, beacon};
        configure_session(l);
    }

    void drain_events() {
        std::vector<EventEnvelope> batch;
        while (subscription->drain(batch, 64) > 0) {
            for (EventEnvelope& event : batch) {
                if (auto* completed = std::any_cast<OpCompletedEvent>(&event.payload)) on_op_completed(*completed);
                else if (const auto* ended = std::any_cast<sessions::SessionEnded>(&event.payload)) on_session_ended(*ended);
            }
            batch.clear();
        }
        if (!subscription->take_resync()) return;
        std::vector<u64> keys;
        for (const auto& launch : launches)
            if (launch->linked_op) keys.push_back(launch->key);
        for (const u64 key : keys) {
            Launch* l = find(key);
            if (l == nullptr || !l->linked_op) continue;
            if (std::optional<ErasedOutcome> outcome = deps.ops.outcome(*l->linked_op)) on_linked_outcome(*l, std::move(*outcome));
        }
    }

    void on_op_completed(OpCompletedEvent& event) {
        const auto it = std::ranges::find(launches, std::optional(event.op), [](const auto& launch) { return launch->linked_op; });
        if (it != launches.end()) on_linked_outcome(**it, std::move(event.outcome));
    }

    void on_session_ended(const sessions::SessionEnded& ended) {
        // ParentEnded: the play session's own stop ended it first.
        if (ended.kind != sessions::SessionKind::Host || !ended.parent || ended.reason == sessions::StopReason::ParentEnded)
            return;
        Launch* l = find(*ended.parent);
        if (l == nullptr || l->stopping || l->linked_op) return;
        (void)deps.sessions.raise_degraded(
            *l->id, make_diag(ErrorDomain::Play, msg::kLinkedServerEnded).severity(Severity::Warning).build());
    }

    // ---- backend session, route, channel ----

    void configure_session(Launch& l) {
        progress(l, "configuring");
        l.session_key = front::SessionKey::generate(deps.random);
        Result<std::string> origin = deps.env.origin(*l.session_key);
        if (!origin) return fail_launch(l, std::move(origin.error()));
        l.origin = std::move(*origin);
        backend::BackendSessionConfig config;
        config.session_key = SecretString{l.session_key->to_hex()};
        config.account_id = identity::account_id(l.in.account);
        config.origin = SecretString{l.origin};
        config.console_key = l.in.settings.values.backend.console_key;
        config.version = *l.in.build->version;
        config.changelist = l.in.build->cl.value_or(Changelist{});
        const u64 key = l.key;
        deps.env.configure_session(l.preflight.lease, std::move(config), guard([this, key](Result<void> configured) {
            Launch* found = live(key);
            if (found == nullptr) return;
            if (!configured) return fail_launch(*found, std::move(configured.error()));
            const auto* minted = std::get_if<identity::BackendMinted>(&found->preflight.login.delivery);
            if (minted == nullptr) return add_route(*found);
            mint_credential(*found, minted->kind);
        }));
    }

    void mint_credential(Launch& l, contracts::backend::CredentialKind kind) {
        backend::LaunchCredentialRequest request{identity::account_id(l.in.account), kind, l.in.build->cl.value_or(Changelist{})};
        const u64 key = l.key;
        deps.env.mint_credential(l.preflight.lease, std::move(request),
                                            guard([this, key](Result<backend::LaunchCredential> credential) {
                                                Launch* found = live(key);
                                                if (found == nullptr) return;
                                                if (!credential) return fail_launch(*found, std::move(credential.error()));
                                                found->credential = std::move(credential->value);
                                                add_route(*found);
                                            }));
    }

    void add_route(Launch& l) {
        front::FrontRoute route{*l.id, *l.session_key, front::EmbeddedUpstream{}, std::move(l.tickets)};
        l.tickets.reset();
        if (!l.in.backend_config.target.embedded()) {
            Result<front::UpstreamOrigin> origin = front::parse_upstream_origin(l.preflight.upstream.origin);
            if (!origin) return fail_launch(l, std::move(origin.error()));
            // The backend already had any plain-http consent before it called the upstream healthy.
            const bool plain = origin->scheme == net::UrlScheme::Http;
            route.upstream = front::ConfiguredUpstream{std::move(*origin), std::nullopt, plain};
        }
        if (Result<void> added = deps.env.add_route(std::move(route)); !added) return fail_launch(l, std::move(added.error()));
        if (l.in.plan.net_mode != injection::NetMode::LegacyFixed) return open_channel(l);

        const injection::LegacyRequirements required = injection::legacy_requirements(l.in.runner.kind);
        front::LegacyFixedRequest request{*l.id, required.xmpp_available(), {}};
        const u64 key = l.key;
        Result<void> opened = deps.env.open_fixed(std::move(request), l.work.token(), guard([this, key](Result<void> bound) {
            Launch* found = live(key);
            if (found == nullptr) return;
            if (!bound) return fail_launch(*found, std::move(bound.error()));
            open_channel(*found);
        }));
        if (!opened) fail_launch(l, std::move(opened.error()));
    }

    [[nodiscard]] game_channel::ClientDllHandlers client_handlers(u64 key) {
        return game_channel::ClientDllHandlers{
            .configure = [this, alive_token = alive.token(),
                          key](const game_channel::ClientDllHello& hello) -> Result<gc::ClientDllConfig> {
                Launch* l = alive_token.cancelled() ? nullptr : live(key);
                if (l == nullptr || !l->dll_config) return std::unexpected(play_diag(msg::kSessionStopping, ErrorKind::Conflict));
                REBOOT_LOG_AT(LogLevel::Info, Play, l->id, "client DLL {} connected from pid {} (game {} cl {})",
                              hello.dll_build, hello.pid, hello.game.version, hello.game.cl);
                return *l->dll_config;
            },
            .on_event = [this, alive_token = alive.token(), key](game_channel::GameLifecycleEvent event) {
                if (!alive_token.cancelled()) on_game_event(key, event);
            },
            .on_liveness = [this, alive_token = alive.token(), key](game_channel::PeerLiveness liveness) {
                if (!alive_token.cancelled()) on_liveness(key, liveness);
            },
            .on_lost = [this, alive_token = alive.token(), key](Diagnostic error) {
                if (!alive_token.cancelled()) on_peer_lost(key, std::move(error));
            },
        };
    }

    [[nodiscard]] gc::ClientDllConfig dll_config(const Launch& l) const {
        gc::ClientDllConfig config;
        config.session_id = l.id->value;
        config.origin = l.origin;
        for (const std::string_view sentinel : kSentinels) config.sentinels.emplace_back(sentinel);
        for (const std::string_view suffix : kRedirectHostSuffixes) config.host_suffixes.emplace_back(suffix);
        config.features = l.preflight.injection.features;
        config.ws_rewrite = config.features.auth_redirect && l.preflight.upstream.websocket.has_value();
        config.console_key = l.in.settings.values.backend.console_key.name;
        config.log_level = dll_log_level();
        return config;
    }

    void open_channel(Launch& l) {
        progress(l, "launching");
        Preflight& pf = l.preflight;
        const components::StoredFile* client = payload_file(l, components::PayloadRole::ClientDll);
        if (client == nullptr) return fail_launch(l, internal_bug("play.open_channel: no client DLL"));
        injection::InjectionInputs inputs;
        inputs.version = *l.in.build->version;
        inputs.build_boot = build_boot(l.in.flags, l.in.runner.kind);
        if (l.in.runner.kind == ports::RunnerKind::Native) inputs.runtime_boot = injection::kNativeBootDefault;
        inputs.client_runtime = injection::PinnedDll{client->path, client->sha256};
        inputs.custom_auth = l.custom_auth;
        pf.injection = injection::plan_injection(inputs);
        pf.build = *l.in.build;
        pf.flags = l.in.flags;
        pf.support = l.in.plan.support;
        pf.untested_confirmed = l.untested_confirmed;
        pf.settings = l.in.settings;
        pf.runner = l.in.runner.kind;
        pf.multiplier = l.in.runner.multiplier();
        l.dll_config = dll_config(l);

        const u64 key = l.key;
        Result<std::unique_ptr<game_channel::ClientDllPeer>> peer = deps.channel.open_client_dll(
            *l.id, std::string(components::payload_file_name(components::PayloadRole::ClientDll)), pf.multiplier,
            client_handlers(key));
        if (!peer) return fail_launch(l, std::move(peer.error()));
        Result<std::string> ctl = deps.channel.ctl_url();
        if (!ctl) return fail_launch(l, std::move(ctl.error()));
        Result<LaunchPlan> plan = launch_plan(l, **peer, std::move(*ctl));
        if (!plan) return fail_launch(l, std::move(plan.error()));
        spawn_game(l, std::move(*plan), std::move(*peer));
    }

    [[nodiscard]] Result<LaunchPlan> launch_plan(Launch& l, const game_channel::ClientDllPeer& peer, std::string ctl) {
        Preflight& pf = l.preflight;
        const storage::PlaySettings& play = l.in.settings.values.play;
        std::vector<GameArg> custom = l.in.custom_args;
        if (pf.wine) map_host_paths(custom, pf.wine->prefix.paths);
        Result<LaunchArgs> args =
            build_launch_args(pf.login, *l.credential, l.in.settings.values.client.game_culture, std::move(custom));
        l.credential.reset();
        if (!args) return std::unexpected(std::move(args.error()));

        process::EnvBuilder game(process::EnvSyntax::Windows);
        // Under Wine the game inherits winhost's environment, so it carries only the channel layer.
        if (!pf.wine)
            game.daemon_base(options.daemon_env)
                .client(l.display)
                .play_settings(play, NativePath{})
                .openssl_ia32cap(pf.flags.openssl_ia32cap);
        game.channel(gc::kEnvCtl, std::move(ctl))
            .channel_secret(gc::kEnvCtlToken, peer.token().env_value())
            .channel(gc::kEnvSession, format_uuid(l.id->value))
            .channel(gc::kEnvRole, std::string(kClientRole));
        Result<process::BuiltEnv> env = std::move(game).build();
        if (!env) return std::unexpected(std::move(env.error()));

        LaunchPlan plan;
        plan.exe = pf.layout.root / pf.layout.shipping_exe;
        plan.cwd = pf.layout.binaries_dir();
        plan.args = std::move(*args);
        plan.env = std::move(*env);
        plan.companions = companions_for(pf.layout);
        plan.inject = pf.injection.inject_entries();
        plan.park = parked_for(pf.layout);
        plan.multiplier = pf.multiplier;
        return plan;
    }

    [[nodiscard]] Result<void> stage_wine(Launch& l) {
        Preflight& pf = l.preflight;
        if (!pf.wine) return {};
        const components::StoredFile* winhost = payload_file(l, components::PayloadRole::Winhost);
        if (winhost == nullptr) return std::unexpected(internal_bug("play.stage_wine: no winhost"));
        process::EnvBuilder env(process::EnvSyntax::Posix);
        env.daemon_base(options.daemon_env)
            .client(l.display)
            .play_settings(l.in.settings.values.play, options.wine_log_dir)
            .openssl_ia32cap(pf.flags.openssl_ia32cap);
        compat::WineSessionSetup setup{pf.wine->runtime.profile.kind,
                                       pf.wine->runtime.layout,
                                       pf.wine->prefix.dir,
                                       pf.wine->prefix.paths,
                                       winhost->path,
                                       std::move(env),
                                       options.wine_log_dir};
        if (Result<void> staged = deps.env.stage_wine(*l.id, std::move(setup)); !staged) return staged;
        l.staged = true;
        return {};
    }

    [[nodiscard]] UniqueFunction<void(ports::SessionHostEvent)> host_events(u64 key) {
        // Win32SessionHost calls from its I/O thread; everything else happens on the strand.
        return [this, alive_token = alive.token(), &strand = deps.strand, key](ports::SessionHostEvent event) {
            strand.post([this, alive_token, key, event = std::move(event)]() mutable {
                if (!alive_token.cancelled()) on_host_event(key, std::move(event));
            });
        };
    }

    void spawn_game(Launch& l, LaunchPlan plan, std::unique_ptr<game_channel::ClientDllPeer> peer) {
        if (Result<void> staged = stage_wine(l); !staged) return fail_launch(l, std::move(staged.error()));
        const u64 key = l.key;
        const SessionId id = *l.id;
        for (const ports::InjectEntry& entry : plan.inject) l.pending_inject.push_back(entry.path);
        l.output = std::make_unique<game_channel::LegacyOutputAdapter>(
            id, game_channel::builtin_lifecycle_markers(),
            [this, alive_token = alive.token(), key](game_channel::GameLifecycleEvent event) {
                // Our DLL drives the phases; output markers only end the session.
                if (!alive_token.cancelled() && std::holds_alternative<game_channel::SessionFatal>(event))
                    on_game_event(key, event);
            });
        l.driver->adopt(std::move(l.preflight));
        l.driver->adopt(std::move(peer));
        if (l.match) deps.match_targets.publish(id, *l.match);

        REBOOT_LOG_AT(LogLevel::Info, Play, l.id, "launching {} {}", display_utf8(plan.exe), to_log_string(plan.args));
        REBOOT_LOG_AT(LogLevel::Debug, Play, l.id, "environment:\n{}", process::to_log_string(plan.env));
        ports::SessionLaunch request = plan.session_launch(id);
        Result<std::unique_ptr<ports::IGameSession>> game = deps.env.session_host().launch(request, host_events(key));
        wipe(request);
        Launch* found = find(key);
        if (found == nullptr) return;
        found->staged = false;
        if (!game) return fail_launch(*found, std::move(game.error()));
        if (found->stopping || found->driver == nullptr) return;
        found->driver->adopt(std::move(*game));
        set_phase(*found, PlayPhase::Launching);
        if (Result<void> resumed = found->driver->game()->resume(); !resumed) return fail_launch(*found, std::move(resumed.error()));
    }

    // ---- the running session ----

    void set_phase(Launch& l, PlayPhase phase) {
        PlaySessionState& state = l.driver->state();
        if (state.phase >= phase) return;
        const PlayPhase before = state.phase;
        state.phase = phase;
        on_phase(l, before);
    }

    void on_phase(Launch& l, PlayPhase before) {
        const PlayPhase phase = l.driver->state().phase;
        (void)deps.sessions.set_phase(*l.id, session_phase(phase));
        switch (phase) {
            case PlayPhase::Loading: progress(l, "loading"); break;
            case PlayPhase::Loaded: progress(l, "loaded"); break;
            case PlayPhase::RedirectReady: progress(l, "redirect_ready"); break;
            default: break;
        }
        if (before < PlayPhase::Loaded && phase >= PlayPhase::Loaded)
            if (Preflight* pf = l.driver->preflight()) pf->holds.clear();
        if (phase == PlayPhase::Running) on_running(l);
    }

    void on_running(Launch& l) {
        REBOOT_LOG_AT(LogLevel::Info, Play, l.id, "the game logged in");
        complete_op(l, Completed<SessionId>{*l.id});
        if (const Preflight* pf = l.driver->preflight()) deps.env.mark_good(*pf);
    }

    // The session ends on its own: before Running the op fails with the same error.
    void end_session(Launch& l, sessions::SessionExit exit) {
        if (l.driver != nullptr) l.driver->state().ending = true;
        if (l.op != nullptr) complete_op(l, Failed{exit.error ? *exit.error : internal_bug("play.end_session: no error")});
        REBOOT_LOG_AT(LogLevel::Info, Play, l.id, "the session ends: {}",
                      sessions::stop_reason_name(sessions::stop_reason_for(exit.reason)));
        deps.sessions.report_exit(*l.id, l.incarnation, std::move(exit));
    }

    [[nodiscard]] Launch* running(u64 key) const {
        Launch* l = live(key);
        return l != nullptr && l->driver != nullptr && !l->driver->state().ending ? l : nullptr;
    }

    void on_game_event(u64 key, const game_channel::GameLifecycleEvent& event) {
        Launch* l = running(key);
        if (l == nullptr) return;
        PlaySessionState& state = l->driver->state();
        const PlayPhase before = state.phase;
        PlayEventEffect effect = apply(state, event);
        if (effect.degraded) (void)deps.sessions.raise_degraded(*l->id, std::move(*effect.degraded));
        if (effect.exit) return end_session(*l, std::move(*effect.exit));
        if (state.phase != before) on_phase(*l, before);
    }

    void on_liveness(u64 key, game_channel::PeerLiveness liveness) {
        Launch* l = running(key);
        if (l == nullptr) return;
        const PlaySessionState& state = l->driver->state();
        REBOOT_LOG_AT(LogLevel::Info, Play, l->id, "the game is {}",
                      liveness == game_channel::PeerLiveness::Responsive ? "responsive" : "unresponsive");
        if (liveness != game_channel::PeerLiveness::Unresponsive) return;
        if (state.phase != PlayPhase::Running || state.traveling) return;
        end_session(*l, sessions::SessionExit{.reason = sessions::ExitReason::Unresponsive, .exit_code = std::nullopt, .error = std::nullopt});
    }

    void on_peer_lost(u64 key, Diagnostic error) {
        Launch* l = running(key);
        if (l == nullptr || l->driver->game_exited()) return;
        end_session(*l, sessions::SessionExit{.reason = sessions::ExitReason::Fatal, .exit_code = std::nullopt, .error = std::move(error)});
    }

    [[nodiscard]] static sessions::SpawnedProcess spawned_process(const Launch& l, ports::SessionRole role, u32 pid) {
        const bool wine = l.in.runner.kind != ports::RunnerKind::Native;
        sessions::SpawnedProcess process;
        process.pid = pid;
        switch (role) {
            case ports::SessionRole::Game: process.role = sessions::ProcessRole::Game; break;
            case ports::SessionRole::Companion: process.role = sessions::ProcessRole::Companion; break;
            case ports::SessionRole::Winhost: process.role = sessions::ProcessRole::Winhost; break;
        }
        // Winhost's children are Windows processes with guest pids; winhost itself is a host process.
        process.space = wine && role != ports::SessionRole::Winhost ? sessions::PidSpace::GuestWindows
                                                                    : sessions::PidSpace::Host;
        return process;
    }

    void on_host_event(u64 key, ports::SessionHostEvent event) {
        Launch* l = find(key);
        if (l == nullptr || !l->id) return;
        std::visit([&](auto& payload) { on_host(*l, payload); }, event);
    }

    void on_host(Launch& l, const ports::Spawned& spawned) {
        const sessions::SpawnedProcess process = spawned_process(l, spawned.role, spawned.pid);
        Result<sessions::Incarnation> incarnation = deps.sessions.note_spawned(*l.id, process);
        if (spawned.role != ports::SessionRole::Game) return;
        if (incarnation) l.incarnation = *incarnation;
        l.game_process = process;
        if (process.space == sessions::PidSpace::Host)
            deps.events.publish(EventKind::ForegroundHint, contracts::ipc::ForegroundHint{spawned.pid},
                                EventScope{.session = l.id, .op = {}, .coalesce_key = {}});
    }

    void on_host(Launch& l, const ports::Injected& injected) {
        if (l.stopping || l.driver == nullptr || l.driver->state().ending) return;
        if (!injected.ok) {
            const Preflight* pf = l.driver->preflight();
            const std::optional<injection::DllSlot> slot = pf != nullptr ? pf->injection.slot_of(injected.path) : std::nullopt;
            Diagnostic error = injection::to_diagnostic(
                injection::InjectFailed{slot.value_or(injection::DllSlot::ClientRuntime), injected.path, injected.error});
            return end_session(l, sessions::SessionExit{.reason = sessions::ExitReason::LaunchFailed,
                                                        .exit_code = std::nullopt,
                                                        .error = std::move(error)});
        }
        // The session host reports the path in its own normal form.
        const NativePath reported = injected.path.lexically_normal();
        const bool planned = std::erase_if(l.pending_inject, [&](const NativePath& path) {
                                 return path.lexically_normal() == reported;
                             }) > 0;
        if (planned && l.pending_inject.empty()) set_phase(l, PlayPhase::Loading);
    }

    void on_host(Launch& l, const ports::Output& output) {
        if (output.role != ports::SessionRole::Game || !l.output) return;
        l.output->feed(output.stream == ports::OutputStream::Stdout ? game_channel::OutputSource::Stdout
                                                                     : game_channel::OutputSource::Stderr,
                       output.bytes);
    }

    void on_host(Launch& l, const ports::Exited& exited) {
        if (exited.role == ports::SessionRole::Companion) return;
        if (exited.role == ports::SessionRole::Game) {
            if (l.output) {
                l.output->finish(game_channel::OutputSource::Stdout);
                l.output->finish(game_channel::OutputSource::Stderr);
            }
            if (l.game_process) deps.sessions.note_process_exited(*l.id, *l.game_process);
            REBOOT_LOG_AT(LogLevel::Info, Play, l.id, "the game exited with {}",
                          exited.code ? std::to_string(*exited.code) : std::string("no code"));
        }
        if (l.driver == nullptr) return;
        const bool ending = l.stopping || l.driver->state().ending;
        // Exited{Winhost} follows HostFatal unless the game exited first, so only the game's exit reports here.
        if (!ending && exited.role == ports::SessionRole::Game) {
            std::optional<i32> code;
            if (exited.code) code = static_cast<i32>(*exited.code);
            end_session(l, exit_for_game_exit(l.driver->state(), code));
        }
        if (l.driver != nullptr) l.driver->on_game_exited();
    }

    void on_host(Launch& l, const ports::HostFatal& fatal) {
        if (l.stopping || l.driver == nullptr || l.driver->state().ending) return;
        end_session(l, sessions::SessionExit{.reason = sessions::ExitReason::Fatal, .exit_code = std::nullopt, .error = fatal.error});
    }

    void on_login_observed(const backend::LoginObservedEvent& event) {
        Launch* l = find(event.session);
        if (l == nullptr) return;
        on_game_event(l->key, game_channel::LoggedIn{});
    }

    [[nodiscard]] std::optional<PlaySessionState> state(const SessionId& session) const {
        Launch* l = find(session);
        if (l == nullptr || l->driver == nullptr) return std::nullopt;
        return l->driver->state();
    }

    PlayCoreDeps deps;
    PlayServiceOptions options;
    std::vector<std::unique_ptr<Launch>> launches;
    u64 last_key = 0;
    std::shared_ptr<Subscription> subscription;
    // Declared last: cancelled first in the destructor, so nothing posted earlier touches the rest.
    CancelSource alive;
};

PlayCore::PlayCore(PlayCoreDeps deps, PlayServiceOptions options)
    : impl_(std::make_unique<Impl>(deps, std::move(options))) {}

PlayCore::~PlayCore() = default;

Result<PlayPlan> PlayCore::plan(const PlayRequest& request) const {
    Result<Inputs> in = impl_->evaluate(request);
    if (!in) return std::unexpected(std::move(in.error()));
    return std::move(in->plan);
}

Result<OpHandle> PlayCore::start(PlayRequest request, DisconnectPolicy policy) {
    return impl_->start(std::move(request), policy);
}

void PlayCore::on_login_observed(const backend::LoginObservedEvent& event) { impl_->on_login_observed(event); }

std::optional<PlaySessionState> PlayCore::state(SessionId session) const { return impl_->state(session); }

}  // namespace rb::play
