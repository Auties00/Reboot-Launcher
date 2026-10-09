#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <any>
#include <chrono>
#include <string>
#include <utility>
#include <vector>

#include "play_test_support.hpp"
#include "reboot/contracts/ipc.hpp"
#include "reboot/foundation/text.hpp"
#include "reboot/play/game_arg.hpp"
#include "reboot/play/play_prompts.hpp"
#include "reboot/sessions/session_driver.hpp"

using namespace rb;
using namespace rb::play;
using namespace rb::play::test;
using namespace std::chrono_literals;

namespace {

constexpr std::string_view kClientLogin = "Player-abc123@projectreboot.dev";

std::string env_value(const ports::EnvBlock& env, std::string_view name) {
    for (const auto& [key, value] : env.vars)
        if (key == name) return value;
    return {};
}

bool has_arg(const std::vector<std::string>& argv, std::string_view text) {
    return std::ranges::find(argv, text) != argv.end();
}

std::string arg_value(const std::vector<std::string>& argv, std::string_view key) {
    for (const std::string& arg : argv)
        if (arg.size() > key.size() && iequals_ascii(std::string_view(arg).substr(0, key.size()), key) && arg[key.size()] == '=')
            return arg.substr(key.size() + 1);
    return {};
}

// A Remote backend at remote.test:3551 over https, which the fake upstream answers as Reboot.
void use_remote_backend(Harness& h) {
    storage::RemoteBackendAddress remote;
    remote.scheme = storage::BackendScheme::Https;
    remote.endpoint = HostPort{"remote.test", Port{3551}};
    h.env.settings_value.values.backend.target.kind = storage::BackendKind::Remote;
    h.env.settings_value.values.backend.target.remote = remote;
    h.env.ready.answer = [] {
        backend::BackendUpstream upstream;
        upstream.origin = "https://remote.test:3551";
        return Result<backend::BackendUpstream>(std::move(upstream));
    };
}

void confirm_untested(Harness& h) {
    h.run();
    REQUIRE(h.answer(UserRequestKind::ConfirmUntested, true));
}

secrets::SecretTarget remote_password() {
    return secrets::SecretTarget{secrets::SecretKind::RemoteBackendPassword,
                                 secrets::SecretScope::backend(HostPort{"remote.test", Port{3551}})};
}

class IdleDriver final : public sessions::ISessionDriver {
public:
    void stop(const sessions::StopRequest&, sessions::StopDone done) override { done(Result<void>{}); }
};

// A child that takes its time to stop, as a linked server in its grace does.
class SlowDriver final : public sessions::ISessionDriver {
public:
    explicit SlowDriver(std::optional<sessions::StopDone>& pending) : pending_(pending) {}
    void stop(const sessions::StopRequest&, sessions::StopDone done) override { pending_ = std::move(done); }

private:
    std::optional<sessions::StopDone>& pending_;
};

}  // namespace

TEST_CASE("a start pins the session, launches the game and completes at LoggedIn", "[play][core]") {
    Harness h;
    const OpHandle op = h.start();
    testing::FakeSessionControl& control = h.launched();
    const std::optional<SessionId> id = h.session();
    REQUIRE(id);
    CHECK(h.phase(*id) == sessions::SessionPhase::Launching);
    CHECK_FALSE(h.outcome(op));

    const ports::SessionLaunch& launch = control.launch();
    const NativePath binaries = build_root() / "FortniteGame/Binaries/Win64";
    CHECK(launch.session == *id);
    CHECK(launch.exe == binaries / std::string(builds::kShippingExe));
    CHECK(launch.cwd == binaries);
    CHECK(launch.multiplier == RunnerMultiplier::Native);
    REQUIRE(launch.args.size() == kFixedGameArgs.size() + 3);
    CHECK(launch.args[9] == "-AUTH_LOGIN=" + std::string(kClientLogin));
    CHECK(launch.args[10] == "-AUTH_PASSWORD=minted-secret");
    CHECK(launch.args[11] == "-AUTH_TYPE=epic");
    REQUIRE(launch.companions.size() == 1);
    CHECK(launch.companions[0].exe == binaries / std::string(builds::kLauncherExe));
    CHECK(launch.park == std::vector<NativePath>{build_root() / "Engine/Binaries/ThirdParty" / std::string(builds::kAftermathDll)});
    REQUIRE(launch.inject.size() == 1);
    CHECK(launch.inject[0].path == client_dll());
    CHECK(launch.inject[0].sha256[0] == 0x11);
    CHECK(launch.inject[0].strategy == ports::BootStrategy::EarlyBirdApc);
    CHECK(launch.inject[0].phase == ports::InjectPhase::Early);
    CHECK(env_value(launch.env, "REBOOT_ROLE") == "client");
    CHECK(env_value(launch.env, "REBOOT_SESSION") == format_uuid(id->value));
    CHECK(env_value(launch.env, "REBOOT_CTL").starts_with("tcp://127.0.0.1:"));
    CHECK(env_value(launch.env, "REBOOT_CTL_TOKEN").size() == 43);
    CHECK(control.resumed());

    REQUIRE(h.env.mint_requests.size() == 1);
    CHECK(h.env.mint_requests[0].account_id == "Player-abc123");
    CHECK(h.env.mint_requests[0].kind == contracts::backend::CredentialKind::LaunchSecret);
    CHECK(h.env.mint_requests[0].build == kChangelist);
    REQUIRE(h.env.configured_sessions.size() == 1);
    const FakePlayEnv::Configured& configured = h.env.configured_sessions[0];
    CHECK(configured.account_id == "Player-abc123");
    CHECK(configured.origin == "http://127.0.0.1:4000/s/" + configured.session_key + "/");
    CHECK(configured.console_key == "F8");
    CHECK(configured.version == kVersion);
    CHECK(configured.changelist == kChangelist);
    REQUIRE(h.env.routes.size() == 1);
    CHECK(h.env.routes[0].session == *id);
    CHECK(h.env.routes[0].embedded);
    CHECK_FALSE(h.env.routes[0].tickets);
    CHECK(h.env.held_roles == std::vector<components::PayloadRole>{components::PayloadRole::ClientDll});
    CHECK(h.env.acquired == 1);
    CHECK(h.env.fixed_requests.empty());

    h.boot(control);
    CHECK(h.phase(*id) == sessions::SessionPhase::Loading);
    CHECK(h.core->state(*id)->phase == PlayPhase::Loading);
    h.recorder.pump();
    CHECK(h.recorder.count(EventKind::ForegroundHint) == 1);
    CHECK(h.recorder.payloads<contracts::ipc::ForegroundHint>(EventKind::ForegroundHint)[0]->pid == 0x2000);

    testing::FakeClientDll& dll = h.connect(control);
    CHECK(h.completed(op) == *id);
    CHECK(h.phase(*id) == sessions::SessionPhase::Running);
    CHECK(h.core->state(*id)->phase == PlayPhase::Running);
    REQUIRE(dll.welcome());
    const gc::ClientDllConfig welcome = *dll.welcome();
    CHECK(welcome.session_id == id->value);
    CHECK(welcome.origin == configured.origin);
    CHECK(welcome.features.auth_redirect);
    CHECK(welcome.features.console);
    CHECK_FALSE(welcome.features.memory_fix);
    CHECK(welcome.ws_rewrite);
    CHECK(welcome.console_key == "F8");
    CHECK_FALSE(welcome.host_suffixes.empty());
    CHECK_FALSE(welcome.sentinels.empty());
    CHECK(h.env.good == 1);
    CHECK_FALSE(h.targets.find(*id));
}

TEST_CASE("a stop shuts our DLL down, stops the game and releases the session", "[play][core]") {
    Harness h;
    const SessionId id = h.running();
    testing::FakeSessionControl& control = *h.host.last();
    testing::FakeClientDll& dll = *h.dlls.back();

    h.stop(id);
    Result<std::vector<gc::GcShutdown>> shutdowns = dll.received<gc::GcShutdown>();
    REQUIRE(shutdowns);
    CHECK(shutdowns->size() == 1);
    CHECK(control.stop_grace() == default_deadline(OpKind::GracefulStop));
    const std::optional<sessions::SessionEnded> ended = h.ended(id);
    REQUIRE(ended);
    CHECK(ended->reason == sessions::StopReason::User);
    CHECK(control.released());
    CHECK(h.env.removed_routes == std::vector<SessionId>{id});
    CHECK(h.env.closed_fixed == std::vector<SessionId>{id});
    CHECK_FALSE(h.core->state(id));
    CHECK(h.registry.list().empty());
}

TEST_CASE("validation failures create neither op nor session", "[play][core][validation]") {
    SECTION("no build selected") {
        Harness h;
        h.env.selected.reset();
        const Result<OpHandle> started = h.core->start(Harness::request());
        REQUIRE_FALSE(started);
        CHECK(started.error().id == "play.no_build_selected");
        CHECK(h.rt.ops().live().empty());
        CHECK(h.registry.list().empty());
    }
    SECTION("an unknown build fails plan and start alike") {
        Harness h;
        PlayRequest request = Harness::request();
        request.build = BuildId{uuid_of(99)};
        CHECK(h.core->plan(request).error().id == "builds.not_found");
        CHECK(h.core->start(request).error().id == "builds.not_found");
    }
    SECTION("a build whose version is not confirmed") {
        Harness h;
        h.env.builds[0].version_source.reset();
        const Result<OpHandle> started = h.core->start(Harness::request());
        REQUIRE_FALSE(started);
        CHECK(started.error().id == "play.build_version_unknown");
        CHECK(arg_text(started.error(), "build") == "Season 12");
    }
    SECTION("another desktop session") {
        Harness h;
        PlayRequest request = Harness::request();
        request.display.os_session = "2";
        CHECK(h.core->start(request).error().id == "play.wrong_session");
        CHECK(h.registry.list().empty());
    }
    SECTION("no display under Display matching") {
        Harness h(PlayServiceOptions{.session_match = SessionMatch::Display, .daemon_env = {}, .wine_log_dir = {}});
        CHECK(h.core->start(Harness::request()).error().id == "play.no_display");
        PlayRequest request = Harness::request();
        request.display.display_env = {{"WAYLAND_DISPLAY", "wayland-0"}};
        CHECK(h.core->start(request));
    }
    SECTION("unparseable custom arguments from the settings or the request") {
        Harness h;
        h.env.settings_value.values.play.custom_args = "-path=\"C:\\x";
        CHECK(h.core->start(Harness::request()).error().id == "play.custom_args_unbalanced_quote");
        h.env.settings_value.values.play.custom_args.clear();
        PlayRequest request = Harness::request();
        request.custom_args = "-AUTH_PASSWORD=mine";
        CHECK(h.core->start(request).error().id == "play.custom_args_reserved");
    }
    SECTION("a runner that cannot run") {
        Harness h;
        h.env.runner_error = failure("compat.no_runtime");
        CHECK(h.core->start(Harness::request()).error().id == "compat.no_runtime");
    }
    SECTION("a Blocked verdict") {
        Harness h;
        h.env.builds[0].version = GameVersion{31, 0, std::nullopt};
        const Result<OpHandle> started = h.core->start(Harness::request());
        REQUIRE_FALSE(started);
        CHECK(started.error().domain == ErrorDomain::Support);
    }
    SECTION("every blocker is reported, the first one leading") {
        Harness h;
        PlayRequest request = Harness::request();
        request.display.os_session = "9";
        request.custom_args = "\"";
        const Result<OpHandle> started = h.core->start(request);
        REQUIRE_FALSE(started);
        CHECK(started.error().id == "play.wrong_session");
        REQUIRE(started.error().causes.size() == 1);
        CHECK(started.error().causes[0].id == "play.custom_args_unbalanced_quote");
    }
    CHECK(true);
}

TEST_CASE("only one play session runs at a time", "[play][core]") {
    Harness h;
    h.untested();
    const OpHandle first = h.start();
    h.run();
    REQUIRE(h.pending(UserRequestKind::ConfirmUntested));
    // A start still asking its questions already counts.
    CHECK(h.core->start(Harness::request()).error().id == "play.session_running");
    CHECK(h.core->plan(Harness::request())->blockers[0].id == "play.session_running");

    REQUIRE(h.answer(UserRequestKind::ConfirmUntested, true));
    testing::FakeSessionControl& control = h.launched();
    h.boot(control);
    h.connect(control);
    const SessionId id = h.completed(first);
    CHECK(h.core->start(Harness::request()).error().id == "play.session_running");

    h.stop(id);
    REQUIRE(h.ended(id));
    CHECK(h.core->start(Harness::request()));
}

TEST_CASE("the plan names its blockers, warnings and the questions start will ask", "[play][core][plan]") {
    SECTION("a tested build plays with no question") {
        Harness h;
        const Result<PlayPlan> plan = h.core->plan(Harness::request());
        REQUIRE(plan);
        CHECK(plan->startable());
        CHECK(plan->build == h.env.builds[0].id);
        CHECK(plan->version == kVersion);
        CHECK(plan->support.tier == support::SupportTier::Tested);
        CHECK(plan->runner == ports::RunnerKind::Native);
        CHECK(plan->net_mode == injection::NetMode::Isolated);
        REQUIRE(plan->login);
        CHECK(plan->login->auth_login == kClientLogin);
        CHECK(plan->decisions.empty());
        CHECK(plan->warnings.empty());
    }
    SECTION("an untested build warns and asks ConfirmUntested") {
        Harness h;
        h.untested();
        const PlayPlan plan = *h.core->plan(Harness::request());
        CHECK(plan.startable());
        CHECK(plan.support.tier == support::SupportTier::Untested);
        CHECK_FALSE(plan.warnings.empty());
        CHECK(plan.decisions == std::vector<RequiredDecision>{{UserRequestKind::ConfirmUntested, std::nullopt}});
    }
    SECTION("targets that ask") {
        Harness h;
        PlayRequest request = Harness::request();
        request.target = AutoServerTarget{};
        CHECK(h.core->plan(request)->decisions ==
              std::vector<RequiredDecision>{{UserRequestKind::AutoServerConsent, std::nullopt}});
        request.target = BrowserServerTarget{ServerId{uuid_of(5)}};
        CHECK(h.core->plan(request)->decisions == std::vector<RequiredDecision>{{UserRequestKind::ConfirmJoin, std::nullopt}});
        // A server picked earlier was confirmed then.
        h.env.join_target_value = browser::JoinTarget{browser::ServerTarget{ServerId{uuid_of(5)}, "Name", "Author"}};
        CHECK(h.core->plan(Harness::request())->decisions.empty());
    }
    SECTION("a password-backed remote login without its secret needs it put") {
        Harness h;
        use_remote_backend(h);
        h.env.remote_login_name = "user@mail.test";
        const PlayPlan plan = *h.core->plan(Harness::request());
        REQUIRE(plan.login);
        CHECK(plan.login->auth_login == "user@mail.test");
        CHECK(std::ranges::find(plan.decisions, RequiredDecision{UserRequestKind::NeedsSecret, remote_password()}) !=
              plan.decisions.end());
        h.env.secrets_present = {remote_password()};
        const PlayPlan with_secret = *h.core->plan(Harness::request());
        CHECK(std::ranges::none_of(with_secret.decisions,
                                   [](const RequiredDecision& d) { return d.kind == UserRequestKind::NeedsSecret; }));
    }
    SECTION("a plain-http remote not yet running asks ConfirmUnencryptedUpstream") {
        Harness h;
        use_remote_backend(h);
        h.env.settings_value.values.backend.target.remote->scheme = storage::BackendScheme::Http;
        const PlayPlan plan = *h.core->plan(Harness::request());
        CHECK(std::ranges::any_of(plan.decisions, [](const RequiredDecision& d) {
            return d.kind == UserRequestKind::ConfirmUnencryptedUpstream;
        }));
    }
    SECTION("a custom auth DLL is LegacyFixed and Untested") {
        Harness h;
        h.env.settings_value.values.play.custom_auth_dll = custom_dll();
        const PlayPlan plan = *h.core->plan(Harness::request());
        CHECK(plan.net_mode == injection::NetMode::LegacyFixed);
        CHECK(plan.support.tier == support::SupportTier::Untested);
        CHECK(plan.support.provider == support::SupportProvider::Custom);
    }
    SECTION("with the linked auto-server the host cell caps the verdict") {
        Harness h;
        h.env.auto_inputs = support::HostInputs{};
        PlayRequest request = Harness::request();
        request.target = AutoServerTarget{};
        const PlayPlan plan = *h.core->plan(request);
        // The described server covers no version, so hosting this build is Blocked.
        CHECK(plan.support.tier == support::SupportTier::Blocked);
        CHECK_FALSE(plan.startable());
    }
}

TEST_CASE("an untested build asks first and a refusal ends the start", "[play][core]") {
    Harness h;
    h.untested();
    const OpHandle op = h.start();
    h.run();
    const std::optional<UserRequest> asked = h.pending(UserRequestKind::ConfirmUntested);
    REQUIRE(asked);
    CHECK(asked->op == op.id());
    const auto* prompt = std::any_cast<UntestedPlayPrompt>(&asked->payload);
    REQUIRE(prompt != nullptr);
    CHECK(prompt->verdict.tier == support::SupportTier::Untested);
    CHECK(prompt->query.version == kVersion);
    CHECK(h.registry.list().empty());

    const Result<void> invalid = h.answer(UserRequestKind::ConfirmUntested, std::string("yes"));
    REQUIRE_FALSE(invalid);
    CHECK(invalid.error().id == "play.invalid_answer");
    REQUIRE(h.answer(UserRequestKind::ConfirmUntested, false));
    CHECK(h.failed(op).id == "play.untested_declined");
    CHECK(h.registry.list().empty());
    CHECK(h.env.layout.calls == 0);
    CHECK(h.core->start(Harness::request()));
}

TEST_CASE("the linked auto-server needs consent, then runs as the session's child", "[play][core][auto]") {
    SECTION("declined") {
        Harness h;
        PlayRequest request = Harness::request();
        request.target = AutoServerTarget{};
        const OpHandle op = h.start(request);
        h.run();
        const std::optional<UserRequest> asked = h.pending(UserRequestKind::AutoServerConsent);
        REQUIRE(asked);
        CHECK(std::any_cast<AutoServerConsentPrompt>(asked->payload).version == kVersion);
        REQUIRE(h.answer(UserRequestKind::AutoServerConsent, false));
        CHECK(h.failed(op).id == "play.auto_server_declined");
        CHECK(h.env.auto_starts.empty());
    }
    SECTION("accepted and listening") {
        Harness h;
        PlayRequest request = Harness::request();
        request.target = AutoServerTarget{};
        const OpHandle op = h.start(request);
        h.run();
        REQUIRE(h.answer(UserRequestKind::AutoServerConsent, true));
        REQUIRE(h.env.auto_starts.size() == 1);
        const std::optional<SessionId> id = h.session();
        REQUIRE(id);
        CHECK(h.env.auto_starts[0].profile == host::kAutoProfileId);
        CHECK(h.env.auto_starts[0].linked_to == id);
        CHECK(h.env.auto_starts[0].overrides.build == h.env.builds[0].id);
        CHECK(h.env.auto_starts[0].disconnect == DisconnectPolicy::Detached);
        CHECK(h.host.last() == nullptr);

        const SessionId server{uuid_of(40)};
        gameserver::BoundSocket game{contracts::game_server::SocketRole::Game, 7790};
        gameserver::BoundSocket beacon{contracts::game_server::SocketRole::Beacon, 7791};
        h.env.listenings[server] = host::HostListening{server, host::PortBlock{Port{7790}, 2}, {game, beacon}};
        h.env.auto_ops[0]->complete(Completed<SessionId>{server});
        testing::FakeSessionControl& control = h.launched();
        const std::optional<MatchTargetEntry> entry = h.targets.find(*id);
        REQUIRE(entry);
        CHECK(*entry == MatchTargetEntry{"Player-abc123", HostPort{"127.0.0.1", Port{7790}}, Port{7791}});
        CHECK(h.targets.resolve({"Player-abc123", "playlist"}).endpoint == HostPort{"127.0.0.1", Port{7790}});
        h.boot(control);
        h.connect(control);
        CHECK(h.completed(op) == *id);
    }
    SECTION("a server that fails to start fails the launch") {
        Harness h;
        PlayRequest request = Harness::request();
        request.target = AutoServerTarget{};
        const OpHandle op = h.start(request);
        h.run();
        REQUIRE(h.answer(UserRequestKind::AutoServerConsent, true));
        const SessionId id = *h.session();
        h.env.auto_ops[0]->complete(Failed{failure("host.listen_timeout")});
        const Diagnostic error = h.failed(op);
        CHECK(error.id == "play.linked_server_failed");
        REQUIRE(error.causes.size() == 1);
        CHECK(error.causes[0].id == "host.listen_timeout");
        CHECK(h.ended(id)->reason == sessions::StopReason::LaunchFailed);
    }
    SECTION("a stop cancels a server still starting") {
        Harness h;
        PlayRequest request = Harness::request();
        request.target = AutoServerTarget{};
        const OpHandle op = h.start(request);
        h.run();
        REQUIRE(h.answer(UserRequestKind::AutoServerConsent, true));
        const SessionId id = *h.session();
        h.stop(id);
        CHECK(std::holds_alternative<Cancelled>(*h.outcome(op)));
        CHECK(std::holds_alternative<Cancelled>(*h.rt.ops().outcome(h.env.auto_ops[0]->id())));
        h.env.auto_ops[0]->complete(Cancelled{CancelReason::User});
        h.run();
        CHECK(h.host.last() == nullptr);
    }
}

TEST_CASE("a linked server that ends first leaves the game running, degraded", "[play][core][auto]") {
    Harness h;
    const SessionId id = h.running();
    sessions::SessionSpec spec;
    spec.kind = sessions::SessionKind::Host;
    spec.parent = id;
    const Result<SessionId> server = h.registry.open(std::move(spec), std::make_unique<IdleDriver>());
    REQUIRE(server);
    h.stop(*server, sessions::StopReason::Crashed);
    const std::vector<Diagnostic> degraded = h.degraded(id);
    REQUIRE(degraded.size() == 1);
    CHECK(degraded[0].id == "play.linked_server_ended");
    CHECK(h.phase(id) == sessions::SessionPhase::Running);
}

TEST_CASE("game targets resolve before the launch and are published for matchmaking", "[play][core][target]") {
    SECTION("a typed address is resolved and probed; a probe failure only degrades") {
        Harness h;
        h.env.resolved.answer = [] {
            browser::CheckedAddress out;
            out.endpoint = Endpoint{IpAddress::v4(0x0A000002), Port{7778}};
            out.probe = browser::ProbeVerdict::Unreachable;
            out.warning = make_diag(ErrorDomain::Browser, MessageId{"browser.target_unreachable"}).severity(Severity::Warning).build();
            return Result<browser::CheckedAddress>(std::move(out));
        };
        PlayRequest request = Harness::request();
        request.target = browser::AddressTarget{"play.example:7778", HostPort{"play.example", Port{7778}}};
        const OpHandle op = h.start(request);
        h.launched();
        const SessionId id = *h.session();
        CHECK(h.env.resolved_addresses == std::vector<HostPort>{HostPort{"play.example", Port{7778}}});
        CHECK(h.targets.find(id) == MatchTargetEntry{"Player-abc123", HostPort{"10.0.0.2", Port{7778}}, std::nullopt});
        const std::vector<Diagnostic> degraded = h.degraded(id);
        REQUIRE(degraded.size() == 1);
        CHECK(degraded[0].id == "browser.target_unreachable");
        CHECK_FALSE(h.outcome(op));
    }
    SECTION("a browser server is joined with ConfirmJoin") {
        Harness h;
        PlayRequest request = Harness::request();
        request.target = BrowserServerTarget{ServerId{uuid_of(5)}};
        h.start(request);
        h.launched();
        REQUIRE(h.env.join_requests.size() == 1);
        CHECK(h.env.join_requests[0].server == ServerId{uuid_of(5)});
        CHECK(h.env.join_requests[0].confirmation == browser::JoinConfirmation::Ask);
        CHECK(h.env.join_requests[0].local_version == kVersion);
        CHECK(h.targets.find(*h.session())->endpoint == HostPort{"10.0.0.1", Port{7777}});
    }
    SECTION("the remembered server was confirmed when it was picked") {
        Harness h;
        h.env.join_target_value = browser::JoinTarget{browser::ServerTarget{ServerId{uuid_of(6)}, "Name", "Author"}};
        h.start();
        h.launched();
        REQUIRE(h.env.join_requests.size() == 1);
        CHECK(h.env.join_requests[0].confirmation == browser::JoinConfirmation::AlreadyConfirmed);
    }
    SECTION("a refused join fails the launch and ends the session") {
        Harness h;
        h.env.join_error = failure("browser.join_own_server");
        PlayRequest request = Harness::request();
        request.target = BrowserServerTarget{ServerId{uuid_of(5)}};
        const OpHandle op = h.start(request);
        CHECK(h.failed(op).id == "browser.join_own_server");
        h.recorder.pump();
        const auto ended = h.recorder.payloads<sessions::SessionEnded>(EventKind::SessionEnded);
        REQUIRE(ended.size() == 1);
        CHECK(ended[0]->reason == sessions::StopReason::LaunchFailed);
        CHECK(h.host.last() == nullptr);
    }
}

TEST_CASE("a preflight failure after the session opened ends it with LaunchFailed", "[play][core]") {
    Harness h;
    h.env.payload.answer = [] { return Result<components::PinnedPayload>(std::unexpected(failure("components.no_payload"))); };
    const OpHandle op = h.start();
    h.run();
    CHECK(h.failed(op).id == "components.no_payload");
    h.recorder.pump();
    const auto ended = h.recorder.payloads<sessions::SessionEnded>(EventKind::SessionEnded);
    REQUIRE(ended.size() == 1);
    CHECK(ended[0]->reason == sessions::StopReason::LaunchFailed);
    REQUIRE(ended[0]->error);
    CHECK(ended[0]->error->id == "components.no_payload");
    CHECK(h.env.acquired == 0);
    CHECK(h.registry.list().empty());
}

TEST_CASE("cancelling the op mid-preflight stops the session and drops the late result", "[play][core][race]") {
    Harness h;
    h.env.payload.hold = true;
    const OpHandle op = h.start();
    h.run();
    const SessionId id = *h.session();
    REQUIRE(h.rt.ops().cancel(op.id(), CancelReason::User));
    h.run();
    CHECK(std::get<Cancelled>(*h.outcome(op)).reason == CancelReason::User);
    CHECK(h.ended(id)->reason == sessions::StopReason::User);
    h.env.payload.release();
    h.run();
    CHECK(h.env.acquired == 0);
    CHECK(h.host.last() == nullptr);
}

TEST_CASE("a preflight that stalls times out and the session fails", "[play][core][race]") {
    Harness h;
    h.env.layout.hold = true;
    const OpHandle op = h.start();
    h.run();
    const SessionId id = *h.session();
    h.rt.advance(default_deadline(OpKind::Play) + 1s);
    CHECK(std::holds_alternative<TimedOut>(*h.outcome(op)));
    const std::optional<sessions::SessionEnded> ended = h.ended(id);
    REQUIRE(ended);
    CHECK(ended->reason == sessions::StopReason::LaunchFailed);
    CHECK(ended->error->id == "play.launch_timed_out");
    h.env.layout.release();
    h.run();
    CHECK(h.env.payload.calls == 0);
}

TEST_CASE("a stop from the registry during preflight cancels the start", "[play][core][race]") {
    Harness h;
    h.env.ready.hold = true;
    const OpHandle op = h.start();
    h.run();
    const SessionId id = *h.session();
    h.stop(id, sessions::StopReason::EngineShutdown);
    CHECK(std::get<Cancelled>(*h.outcome(op)).reason == CancelReason::Shutdown);
    CHECK(h.ended(id)->reason == sessions::StopReason::EngineShutdown);
    h.env.ready.release();
    h.run();
    CHECK(h.env.configured_sessions.empty());
    CHECK(h.host.last() == nullptr);
}

TEST_CASE("a game that exits or fails before login fails the launch", "[play][core]") {
    SECTION("exited") {
        Harness h;
        const OpHandle op = h.start();
        testing::FakeSessionControl& control = h.launched();
        const SessionId id = *h.session();
        h.boot(control);
        control.game_exits(0);
        CHECK(h.failed(op).id == "play.exited_before_login");
        const std::optional<sessions::SessionEnded> ended = h.ended(id);
        CHECK(ended->reason == sessions::StopReason::Exited);
        CHECK(ended->exit_code == 0);
    }
    SECTION("an injection failed") {
        Harness h;
        const OpHandle op = h.start();
        testing::FakeSessionControl& control = h.launched();
        const SessionId id = *h.session();
        control.spawn_all();
        control.emit(ports::Injected{client_dll(), false, SystemError{SystemError::Origin::Host, 5}});
        h.run();
        const Diagnostic error = h.failed(op);
        CHECK(error.id == "injection.inject_failed");
        CHECK(h.ended(id)->reason == sessions::StopReason::LaunchFailed);
        CHECK(h.phase(id) == std::nullopt);
    }
    SECTION("the session host failed") {
        Harness h;
        const OpHandle op = h.start();
        testing::FakeSessionControl& control = h.launched();
        const SessionId id = *h.session();
        control.emit(ports::HostFatal{failure("compat.winhost_fatal")});
        h.run();
        CHECK(h.failed(op).id == "compat.winhost_fatal");
        CHECK(h.ended(id)->reason == sessions::StopReason::Fatal);
    }
    SECTION("the launch itself failed") {
        Harness h;
        h.host.faults().fail_next(testing::SessionHostOperation::Launch, failure("platform.spawn_failed"));
        const OpHandle op = h.start();
        CHECK(h.failed(op).id == "platform.spawn_failed");
        CHECK(h.registry.list().empty());
    }
    SECTION("our DLL reported a fatal step") {
        Harness h;
        const OpHandle op = h.start();
        testing::FakeSessionControl& control = h.launched();
        const SessionId id = *h.session();
        h.boot(control);
        testing::FakeClientDllScript script;
        script.after_welcome = {gc::Loaded{"fake", 1, gc::BuildMatch::Exact}, gc::Fatal{"unpack"}};
        h.connect(control, std::move(script));
        const Diagnostic error = h.failed(op);
        CHECK(error.id == "play.fatal");
        CHECK(arg_text(error, "step") == "unpack");
        CHECK(h.ended(id)->reason == sessions::StopReason::Fatal);
    }
    SECTION("a corrupt-build marker in the game output") {
        Harness h;
        const OpHandle op = h.start();
        testing::FakeSessionControl& control = h.launched();
        const SessionId id = *h.session();
        h.boot(control);
        const std::string line = "LogWindows:Error: Fatal error!\n";
        control.emit(ports::Output{ports::SessionRole::Game, ports::OutputStream::Stdout, std::vector<u8>(line.begin(), line.end())});
        h.run();
        CHECK(h.failed(op).id == "play.corrupt_build");
        CHECK(h.ended(id)->reason == sessions::StopReason::Fatal);
    }
}

TEST_CASE("a running session ends on its own as the game says", "[play][core][running]") {
    SECTION("an exit request is a clean exit") {
        Harness h;
        const SessionId id = h.running();
        h.dlls.back()->send(gc::ExitRequested{gc::ExitKind::RequestExit, 0, ""});
        h.run();
        CHECK(h.ended(id)->reason == sessions::StopReason::Exited);
    }
    SECTION("an NTSTATUS exit code is a crash") {
        Harness h;
        const SessionId id = h.running();
        h.host.last()->game_exits(static_cast<int>(0xC0000005u));
        const std::optional<sessions::SessionEnded> ended = h.ended(id);
        CHECK(ended->reason == sessions::StopReason::Crashed);
        CHECK(ended->error->id == "play.crashed");
    }
    SECTION("losing our DLL is fatal") {
        Harness h;
        const SessionId id = h.running();
        h.dlls.back()->disconnect();
        h.run();
        const std::optional<sessions::SessionEnded> ended = h.ended(id);
        CHECK(ended->reason == sessions::StopReason::Fatal);
        CHECK(ended->error->id == "game_channel.peer_lost");
    }
    SECTION("a hung game is not reported while it travels") {
        Harness h;
        const OpHandle op = h.start();
        testing::FakeSessionControl& control = h.launched();
        h.boot(control);
        testing::FakeClientDllScript script;
        script.after_welcome = {gc::Loaded{"fake", 1, gc::BuildMatch::Exact}, gc::LoggedIn{}, testing::ScriptStopPonging{}};
        h.connect(control, std::move(script));
        const SessionId id = h.completed(op);
        h.dlls.back()->send(gc::TravelStarted{});
        h.run();
        h.rt.advance(kLivenessPingInterval * 6);
        CHECK(h.phase(id) == sessions::SessionPhase::Running);
        CHECK(h.core->state(id)->traveling);
    }
    SECTION("an unresponsive game while Running ends the session") {
        Harness h;
        const OpHandle op = h.start();
        testing::FakeSessionControl& control = h.launched();
        h.boot(control);
        testing::FakeClientDllScript script;
        script.after_welcome = {gc::Loaded{"fake", 1, gc::BuildMatch::Exact}, gc::LoggedIn{}, testing::ScriptStopPonging{}};
        h.connect(control, std::move(script));
        const SessionId id = h.completed(op);
        h.rt.advance(kLivenessPingInterval * 6);
        CHECK(h.ended(id)->reason == sessions::StopReason::Unresponsive);
    }
}

TEST_CASE("the backend's login observation stands in for a missing LoggedIn", "[play][core]") {
    Harness h;
    const OpHandle op = h.start();
    testing::FakeSessionControl& control = h.launched();
    h.boot(control);
    testing::FakeClientDllScript script;
    script.after_welcome = {gc::Loaded{"fake", 1, gc::BuildMatch::Exact}, gc::RedirectReady{}};
    h.connect(control, std::move(script));
    const SessionId id = *h.session();
    CHECK(h.core->state(id)->phase == PlayPhase::RedirectReady);
    CHECK_FALSE(h.outcome(op));
    h.core->on_login_observed(backend::LoginObservedEvent{id, "Player-abc123"});
    CHECK(h.completed(op) == id);
    // Unknown sessions are ignored.
    h.core->on_login_observed(backend::LoginObservedEvent{SessionId{uuid_of(77)}, "x"});
}

TEST_CASE("failed optional patches are raised as degraded features", "[play][core]") {
    Harness h;
    const OpHandle op = h.start();
    testing::FakeSessionControl& control = h.launched();
    h.boot(control);
    testing::FakeClientDllScript script;
    script.after_welcome = {gc::Loaded{"fake", 1, gc::BuildMatch::Exact},
                            gc::PatchResult{"memory_fix", gc::PatchStatus::Failed, 0}, gc::LoggedIn{}};
    h.connect(control, std::move(script));
    const SessionId id = h.completed(op);
    const std::vector<Diagnostic> degraded = h.degraded(id);
    REQUIRE(degraded.size() == 1);
    CHECK(degraded[0].id == "play.features_degraded");
    CHECK(arg_text(degraded[0], "features") == "memory_fix");
    CHECK(h.core->state(id)->degraded == std::vector<std::string>{"memory_fix"});
}

TEST_CASE("a custom auth DLL runs LegacyFixed after ours, with the fixed listeners", "[play][core][legacy]") {
    Harness h;
    h.env.settings_value.values.play.custom_auth_dll = custom_dll();
    const OpHandle op = h.start();
    confirm_untested(h);
    testing::FakeSessionControl& control = h.launched();
    const SessionId id = *h.session();
    CHECK(h.env.verified == std::vector<NativePath>{custom_dll()});
    REQUIRE(h.env.fixed_requests.size() == 1);
    CHECK(h.env.fixed_requests[0].session == id);
    CHECK(h.env.fixed_requests[0].xmpp);
    REQUIRE(control.launch().inject.size() == 2);
    CHECK(control.launch().inject[0].path == client_dll());
    CHECK(control.launch().inject[1].path == custom_dll());
    CHECK(h.core->state(id)->net_mode == injection::NetMode::LegacyFixed);
    h.boot(control);
    testing::FakeClientDll& dll = h.connect(control);
    CHECK(h.completed(op) == id);
    CHECK_FALSE(dll.welcome()->features.auth_redirect);
    CHECK_FALSE(dll.welcome()->ws_rewrite);
}

TEST_CASE("a busy fixed port fails the LegacyFixed launch", "[play][core][legacy]") {
    Harness h;
    h.env.settings_value.values.play.custom_auth_dll = custom_dll();
    h.env.fixed.answer = [] { return Result<void>(std::unexpected(failure("net.port_busy"))); };
    const OpHandle op = h.start();
    confirm_untested(h);
    CHECK(h.failed(op).id == "net.port_busy");
    CHECK(h.host.last() == nullptr);
}

TEST_CASE("remote backends get the credential their login plan names", "[play][core][credential]") {
    SECTION("a Reboot upstream with a login hands the game an exchange code") {
        Harness h;
        use_remote_backend(h);
        h.env.remote_login_name = "user@mail.test";
        h.env.flags.auth_exchangecode = true;
        h.env.login_request = RequestId{5};
        h.start();
        confirm_untested(h);
        testing::FakeSessionControl& control = h.launched();
        REQUIRE(h.env.logins.size() == 1);
        CHECK(h.env.logins[0].login == "user@mail.test");
        CHECK(h.env.logins[0].want_exchange_code);
        CHECK(h.env.logins[0].upstream.url.host == "remote.test");
        CHECK(h.env.logins[0].upstream.url.scheme == net::UrlScheme::Https);
        CHECK(h.env.logins[0].wait.session == h.session());
        CHECK(arg_value(control.launch().args, "-AUTH_LOGIN") == "user@mail.test");
        CHECK(arg_value(control.launch().args, "-AUTH_PASSWORD") == "exchange-code");
        CHECK(arg_value(control.launch().args, "-AUTH_TYPE") == "exchangecode");
        CHECK(h.env.mint_requests.empty());
        REQUIRE(h.env.routes.size() == 1);
        CHECK_FALSE(h.env.routes[0].embedded);
        CHECK(h.env.routes[0].upstream->host == "remote.test");
        CHECK_FALSE(h.env.routes[0].plain_http);
    }
    SECTION("otherwise the front swaps a session ticket for the stored password") {
        Harness h;
        use_remote_backend(h);
        h.env.remote_login_name = "user@mail.test";
        h.start();
        confirm_untested(h);
        testing::FakeSessionControl& control = h.launched();
        REQUIRE(h.env.logins.size() == 1);
        CHECK_FALSE(h.env.logins[0].want_exchange_code);
        CHECK(h.env.provided == std::vector<secrets::SecretTarget>{remote_password()});
        REQUIRE(h.env.routes.size() == 1);
        CHECK(h.env.routes[0].tickets);
        const std::string ticket = arg_value(control.launch().args, "-AUTH_PASSWORD");
        CHECK(ticket.size() == 64);
        CHECK(ticket != h.env.stored_password);
        CHECK(arg_value(control.launch().args, "-AUTH_TYPE") == "epic");
    }
    SECTION("without a login the ticket passes through") {
        Harness h;
        use_remote_backend(h);
        h.start();
        confirm_untested(h);
        testing::FakeSessionControl& control = h.launched();
        CHECK(h.env.logins.empty());
        CHECK_FALSE(h.env.routes[0].tickets);
        CHECK(arg_value(control.launch().args, "-AUTH_PASSWORD").size() == 64);
        CHECK(arg_value(control.launch().args, "-AUTH_LOGIN") == kClientLogin);
    }
    SECTION("LegacyArgv puts the stored password in argv, asking for it when missing") {
        Harness h;
        use_remote_backend(h);
        h.env.remote_login_name = "user@mail.test";
        h.env.credential_policy = identity::CredentialPolicy::LegacyArgv;
        h.env.settings_value.values.play.custom_auth_dll = custom_dll();
        h.env.required.hold = true;
        h.env.secret_request = RequestId{9};
        const OpHandle op = h.start();
        confirm_untested(h);
        REQUIRE(h.env.required_targets == std::vector<secrets::SecretTarget>{remote_password()});
        CHECK(h.env.required_waits[0].reason == secrets::NeedsSecretReason::Missing);
        CHECK(h.env.required_waits[0].op == op.id());
        const std::vector<LiveOp> live = h.rt.ops().live();
        REQUIRE(live.size() == 1);
        h.env.required.release();
        testing::FakeSessionControl& control = h.launched();
        CHECK(arg_value(control.launch().args, "-AUTH_PASSWORD") == "argv-password");
        CHECK(h.env.routes[0].tickets == false);
    }
    SECTION("a failed remote login fails the launch") {
        Harness h;
        use_remote_backend(h);
        h.env.remote_login_name = "user@mail.test";
        h.env.logged_in.answer = [] {
            return Result<backend::RemoteLoginResult>(std::unexpected(failure("secrets.request_withdrawn")));
        };
        const OpHandle op = h.start();
        confirm_untested(h);
        CHECK(h.failed(op).id == "secrets.request_withdrawn");
    }
}

TEST_CASE("a backend named in the request reconfigures the backend first", "[play][core]") {
    SECTION("reconfigured") {
        Harness h;
        PlayRequest request = Harness::request();
        request.backend = backend::BackendTarget{backend::LocalBackend{}};
        h.start(request);
        confirm_untested(h);
        h.launched();
        REQUIRE(h.env.reconfigured.size() == 1);
        CHECK(h.env.reconfigured[0].target.kind() == storage::BackendKind::Local);
    }
    SECTION("refused") {
        Harness h;
        h.env.reconfigure_error = failure("backend.in_use");
        PlayRequest request = Harness::request();
        request.backend = backend::BackendTarget{backend::LocalBackend{}};
        const OpHandle op = h.start(request);
        confirm_untested(h);
        CHECK(h.failed(op).id == "backend.in_use");
        CHECK(h.env.acquired == 0);
    }
    SECTION("already on it") {
        Harness h;
        PlayRequest request = Harness::request();
        request.backend = backend::BackendTarget{backend::EmbeddedBackend{}};
        h.start(request);
        h.launched();
        CHECK(h.env.reconfigured.empty());
    }
}

TEST_CASE("under Wine the runtime and prefix are pinned and winhost carries the environment", "[play][core][wine]") {
    Harness h;
    h.env.runner_value = RunnerChoice{ports::RunnerKind::Wine,
                                      compat::RunnerProfile{compat::RunnerKind::Wine, compat::RuntimeId{"kron-10"}, std::nullopt, false}};
    h.env.policy.set_inputs(Harness::tested_inputs(ports::RunnerKind::Wine, "kron-10"));
    const std::u8string map = (build_root() / "maps" / "x.umap").lexically_normal().u8string();
    h.env.settings_value.values.play.custom_args = "-map=\"" + std::string(map.begin(), map.end()) + "\"";
    const OpHandle op = h.start();
    testing::FakeSessionControl& control = h.launched();
    const SessionId id = *h.session();
    CHECK(h.registry.get(id)->pinned.runtime_id == std::string("kron-10"));
    CHECK(h.env.runtime_profiles.size() == 1);
    CHECK(h.env.prefix_dlls == std::vector<NativePath>{client_dll()});
    CHECK(h.env.held_roles.empty());
    REQUIRE(h.env.staged.size() == 1);
    CHECK(h.env.staged[0].session == id);
    CHECK(h.env.staged[0].winhost == winhost_exe());
    CHECK(h.env.staged[0].prefix == root() / "prefixes" / "wine");
    const ports::SessionLaunch& launch = control.launch();
    CHECK(launch.multiplier == RunnerMultiplier::Wine);
    CHECK(launch.env.vars.size() == 4);
    CHECK(arg_value(launch.args, "-map") == R"(Z:\12.41\maps\x.umap)");
    REQUIRE(launch.inject.size() == 1);
    CHECK(launch.inject[0].strategy == ports::BootStrategy::AfterResume);

    h.boot(control);
    const sessions::SessionInfo info = *h.registry.get(id);
    REQUIRE_FALSE(info.processes.empty());
    CHECK(info.processes[0].space == sessions::PidSpace::GuestWindows);
    h.recorder.pump();
    CHECK(h.recorder.count(EventKind::ForegroundHint) == 0);
    h.connect(control);
    CHECK(h.completed(op) == id);
    CHECK(h.env.good == 1);
}

TEST_CASE("a stop during Wine preflight never stages the session", "[play][core][wine]") {
    Harness h;
    h.env.runner_value = RunnerChoice{ports::RunnerKind::Wine,
                                      compat::RunnerProfile{compat::RunnerKind::Wine, compat::RuntimeId{"kron-10"}, std::nullopt, false}};
    h.env.policy.set_inputs(Harness::tested_inputs(ports::RunnerKind::Wine, "kron-10"));
    h.env.prefix.hold = true;
    const OpHandle op = h.start();
    h.run();
    const SessionId id = *h.session();
    h.stop(id);
    CHECK(std::holds_alternative<Cancelled>(*h.outcome(op)));
    h.env.prefix.release();
    h.run();
    CHECK(h.env.staged.empty());
}

TEST_CASE("destroying the core releases its sessions and a later stop ends at once", "[play][core]") {
    Harness h;
    const SessionId id = h.running();
    h.core.reset();
    CHECK(h.env.removed_routes == std::vector<SessionId>{id});
    CHECK(h.host.last()->released());
    h.stop(id);
    CHECK(h.ended(id));
}

TEST_CASE("a linked server ended by the session's own stop raises nothing", "[play][core][auto]") {
    Harness h;
    const SessionId id = h.running();
    sessions::SessionSpec spec;
    spec.kind = sessions::SessionKind::Host;
    spec.parent = id;
    const Result<SessionId> server = h.registry.open(std::move(spec), std::make_unique<IdleDriver>());
    REQUIRE(server);
    h.stop(id);
    REQUIRE(h.ended(*server));
    CHECK(h.ended(*server)->reason == sessions::StopReason::ParentEnded);
    REQUIRE(h.ended(id));
    CHECK(h.degraded(id).empty());
}

TEST_CASE("a cancel while the linked server still stops ends the start at once", "[play][core][race]") {
    Harness h;
    h.env.configured.hold = true;
    PlayRequest request = Harness::request();
    request.target = AutoServerTarget{};
    const OpHandle op = h.start(request);
    h.run();
    REQUIRE(h.answer(UserRequestKind::AutoServerConsent, true));
    const SessionId id = *h.session();
    std::optional<sessions::StopDone> child_stop;
    sessions::SessionSpec spec;
    spec.kind = sessions::SessionKind::Host;
    spec.parent = id;
    const Result<SessionId> server = h.registry.open(std::move(spec), std::make_unique<SlowDriver>(child_stop));
    REQUIRE(server);
    gameserver::BoundSocket game{contracts::game_server::SocketRole::Game, 7790};
    h.env.listenings[*server] = host::HostListening{*server, host::PortBlock{Port{7790}, 2}, {game}};
    h.env.auto_ops[0]->complete(Completed<SessionId>{*server});
    h.run();
    REQUIRE(h.env.configured.pending.size() == 1);

    REQUIRE(h.rt.ops().cancel(op.id(), CancelReason::User));
    h.run();
    REQUIRE(child_stop);
    // The backend answers while the registry still waits for the server.
    h.env.configured.release();
    h.run();
    CHECK(h.env.routes.empty());
    CHECK(h.host.last() == nullptr);
    (*child_stop)(Result<void>{});
    CHECK(h.ended(id)->reason == sessions::StopReason::User);
}

TEST_CASE("a login racing a cancel does not keep the session", "[play][core][race]") {
    Harness h;
    const OpHandle op = h.start();
    testing::FakeSessionControl& control = h.launched();
    h.boot(control);
    testing::FakeClientDllScript script;
    script.after_welcome = {gc::Loaded{"fake", 1, gc::BuildMatch::Exact}, gc::RedirectReady{}};
    h.connect(control, std::move(script));
    const SessionId id = *h.session();
    REQUIRE(h.rt.ops().cancel(op.id(), CancelReason::User));
    // Before the posted cancel handler runs.
    h.core->on_login_observed(backend::LoginObservedEvent{id, "Player-abc123"});
    CHECK(std::get<Cancelled>(*h.outcome(op)).reason == CancelReason::User);
    const std::optional<sessions::SessionEnded> ended = h.ended(id);
    REQUIRE(ended);
    CHECK(ended->reason == sessions::StopReason::User);
    CHECK(h.env.good == 0);
}

TEST_CASE("destroying the core mid-preflight cancels its op and pending steps", "[play][core]") {
    Harness h;
    h.env.layout.hold = true;
    const OpHandle op = h.start();
    h.run();
    REQUIRE(h.env.layout.pending.size() == 1);
    h.core.reset();
    CHECK(h.env.layout_token.cancelled());
    CHECK(std::get<Cancelled>(*h.outcome(op)).reason == CancelReason::Shutdown);
}
