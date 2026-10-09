#include <catch2/catch_test_macros.hpp>

#include <any>
#include <chrono>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "backend_test_support.hpp"
#include "reboot/backend/backend_service.hpp"
#include "reboot/backend/backend_sessions.hpp"
#include "reboot/backend/remote_backend_probe.hpp"
#include "reboot/backend/unencrypted_upstream_prompt.hpp"
#include "reboot/net/host_tls_memory.hpp"
#include "reboot/net/http_client.hpp"
#include "reboot/testing/fake_http_transport.hpp"
#include "reboot/testing/fake_random.hpp"

using namespace reboot;
using namespace reboot::backend;
using namespace reboot::backend::test;
using namespace std::chrono_literals;

namespace be = reboot::contracts::backend;

namespace {

struct FakeSessions final : IBackendSessions {
    void stop_sessions(std::vector<SessionId> sessions, UniqueFunction<void(Result<void>)> done) override {
        asked.push_back(std::move(sessions));
        pending.push_back(std::move(done));
    }

    std::vector<std::vector<SessionId>> asked;
    std::vector<UniqueFunction<void(Result<void>)>> pending;
};

struct ServiceHarness : ProcessHarness {
    explicit ServiceHarness(BackendConfig config = {}) {
        service = std::make_unique<BackendService>(std::move(config), *process, probe, sessions, rt.ops(), rt.requests(),
                                                   tls, rt.events(), rt.strand(), rt.timers());
    }

    [[nodiscard]] const BackendState& state() const { return service->state(); }

    [[nodiscard]] BackendLease lease(u8 session) {
        Result<BackendLease> acquired = service->acquire(session_id(session));
        REQUIRE(acquired);
        return std::move(*acquired);
    }

    [[nodiscard]] std::optional<ErasedOutcome> outcome(const Result<OpHandle>& handle) {
        REQUIRE(handle);
        return rt.ops().outcome(handle->id());
    }

    [[nodiscard]] std::optional<BackendUpstream> upstream_of(const Result<OpHandle>& handle) {
        const std::optional<ErasedOutcome> done = outcome(handle);
        if (!done) return std::nullopt;
        const auto* completed = std::get_if<Completed<std::any>>(&*done);
        if (completed == nullptr) return std::nullopt;
        return std::any_cast<BackendUpstream>(completed->value);
    }

    testing::FakeHttpTransport transport{rt.strand(), rt.clock()};
    testing::FakeRandom random{7};
    net::HostTlsMemory tls{{}, nullptr};
    net::HttpClient http{transport, tls, rt.strand(), rt.timers(), random};
    RemoteBackendProbe probe{http};
    FakeSessions sessions;
    BackendEvents events{rt.events()};
    std::unique_ptr<BackendService> service;
};

[[nodiscard]] testing::FakeHttpResponse answer(u32 status, std::string body = {}) {
    testing::FakeHttpResponse response;
    response.status = status;
    response.body.assign(body.begin(), body.end());
    return response;
}

[[nodiscard]] BackendConfig remote_config(std::optional<net::UrlScheme> scheme = std::nullopt) {
    return BackendConfig{BackendTarget{RemoteBackend{BackendUrl{scheme, "play.example", Port{3551}}, std::nullopt}}, false};
}

[[nodiscard]] BackendConfig local_config() {
    return BackendConfig{
        BackendTarget{LocalBackend{HostPort{"127.0.0.1", Port{3551}}, HostPort{"127.0.0.1", std::nullopt}}}, false};
}

constexpr std::string_view kRebootInfo = R"({"impl":"reboot","api_version":1,"version":"2.0","ws_port":8443})";

[[nodiscard]] std::optional<UserRequest> pending_request(const UserRequestRegistry& requests, UserRequestKind kind) {
    for (const UserRequest& request : requests.pending())
        if (request.kind == kind) return request;
    return std::nullopt;
}

[[nodiscard]] LaunchCredentialRequest exchange_request() {
    return LaunchCredentialRequest{"Player-abc123", be::CredentialKind::ExchangeCode, Changelist{1}};
}

}  // namespace

TEST_CASE("the first lease starts the backend and the last one stops it", "[backend][service]") {
    ServiceHarness h;
    BackendLease lease = h.lease(1);
    CHECK(lease.active());
    CHECK(lease.session() == session_id(1));
    CHECK(h.state().phase == BackendPhase::Starting);
    CHECK(h.state().leases == 1);
    CHECK(h.service->session_leases() == 1);

    Captured<Result<BackendUpstream>> ready;
    h.service->ensure_ready(lease, {}, ready.sink());
    h.rt.run_until_idle();
    REQUIRE(ready.value);
    REQUIRE(ready.value->has_value());
    const BackendUpstream& upstream = **ready.value;
    CHECK(upstream.origin == "http://127.0.0.1:1");
    CHECK(upstream.flavor == identity::UpstreamFlavor::Reboot);
    CHECK(upstream.websocket == HostPort{"127.0.0.1", Port{1}});
    CHECK(upstream.http_port == 1);
    CHECK(h.state().phase == BackendPhase::Running);
    CHECK(h.state().generation == 1);
    CHECK(h.state().version == "fake");
    CHECK(h.state().upstream == upstream);

    lease.release();
    CHECK_FALSE(lease.active());
    h.rt.run_until_idle();
    CHECK(h.state().phase == BackendPhase::Stopped);
    CHECK(h.state().leases == 0);
    CHECK(h.process->state() == process::ChildState::Stopped);
    REQUIRE_FALSE(h.events.all().empty());
    CHECK(h.events.all().back().change == BackendChange::Stopped);
    CHECK(h.events.all().back().stop_cause == BackendStopCause::LastLease);
    CHECK(h.events.saw(BackendChange::Starting));
    CHECK(h.events.saw(BackendChange::Running));
    CHECK(h.events.saw(BackendChange::Stopping));
    CHECK_FALSE(h.events.saw(BackendChange::Crashed));
}

TEST_CASE("a pinned backend outlives its leases, and the user cannot stop it under a session", "[backend][service]") {
    ServiceHarness h;
    const Result<OpHandle> start = h.service->start_backend(true, DisconnectPolicy::Detached);
    h.rt.run_until_idle();
    const std::optional<BackendUpstream> upstream = h.upstream_of(start);
    REQUIRE(upstream);
    CHECK(upstream->http_port == 1);
    CHECK(h.state().pinned);

    {
        BackendLease lease = h.lease(1);
        const Result<OpHandle> refused = h.service->start_stop(DisconnectPolicy::Detached);
        REQUIRE_FALSE(refused);
        CHECK(refused.error().id == "backend.in_use");
    }
    h.rt.run_until_idle();
    CHECK(h.state().phase == BackendPhase::Running);

    const Result<OpHandle> stop = h.service->start_stop(DisconnectPolicy::Detached);
    h.rt.run_until_idle();
    const std::optional<ErasedOutcome> stopped = h.outcome(stop);
    REQUIRE(stopped);
    CHECK(std::holds_alternative<Completed<std::any>>(*stopped));
    CHECK(h.state().phase == BackendPhase::Stopped);
    CHECK_FALSE(h.state().pinned);
    CHECK(h.events.all().back().stop_cause == BackendStopCause::UserStop);
}

TEST_CASE("a stop during a start supersedes the start op", "[backend][service]") {
    ServiceHarness h;
    h.script.ready_delay = 10s;
    const Result<OpHandle> start = h.service->start_backend(false, DisconnectPolicy::Detached);
    h.rt.run_until_idle();
    CHECK(h.state().phase == BackendPhase::Starting);

    const Result<OpHandle> stop = h.service->start_stop(DisconnectPolicy::Detached);
    h.rt.run_until_idle();
    const std::optional<ErasedOutcome> superseded = h.outcome(start);
    REQUIRE(superseded);
    const auto* cancelled = std::get_if<Cancelled>(&*superseded);
    REQUIRE(cancelled != nullptr);
    CHECK(cancelled->reason == CancelReason::Superseded);
    const std::optional<ErasedOutcome> stopped = h.outcome(stop);
    REQUIRE(stopped);
    CHECK(std::holds_alternative<Completed<std::any>>(*stopped));
    CHECK(h.state().phase == BackendPhase::Stopped);
}

TEST_CASE("a start during a start joins it", "[backend][service]") {
    ServiceHarness h;
    h.script.ready_delay = 1s;
    const Result<OpHandle> first = h.service->start_backend(false, DisconnectPolicy::Detached);
    const Result<OpHandle> second = h.service->start_backend(false, DisconnectPolicy::Detached);
    BackendLease lease = h.lease(1);
    h.rt.run_until_idle();
    CHECK_FALSE(h.outcome(first));
    h.rt.advance(1s);
    CHECK(h.upstream_of(first));
    CHECK(h.upstream_of(second));
    CHECK(h.launcher.children().size() == 1);
}

TEST_CASE("a start queued behind a stop runs after it", "[backend][service]") {
    ServiceHarness h;
    BackendLease first = h.lease(1);
    h.rt.run_until_idle();
    REQUIRE(h.state().phase == BackendPhase::Running);
    first.release();
    CHECK(h.state().phase == BackendPhase::Stopping);
    BackendLease second = h.lease(2);
    Captured<Result<BackendUpstream>> ready;
    h.service->ensure_ready(second, {}, ready.sink());
    h.rt.run_until_idle();
    REQUIRE(ready.value);
    CHECK(ready.value->has_value());
    CHECK(h.state().phase == BackendPhase::Running);
    CHECK(h.state().generation == 2);
}

TEST_CASE("a crash restarts the backend and waiters are served after the restart", "[backend][service]") {
    ServiceHarness h;
    testing::FakeBackendScript crashing;
    crashing.child.crash = testing::ScriptedFailure{testing::ScriptStage::Ready, 1s};
    h.scripts.push_back(crashing);
    BackendLease lease = h.lease(1);
    h.rt.run_until_idle();
    REQUIRE(h.state().phase == BackendPhase::Running);

    h.rt.advance(1s);
    CHECK(h.state().phase == BackendPhase::Restarting);
    REQUIRE(h.state().last_error);
    CHECK(h.state().last_error->id == "backend.crashed");
    CHECK(h.events.saw(BackendChange::Crashed));
    CHECK(h.events.saw(BackendChange::Restarting));
    CHECK_FALSE(h.state().upstream);

    Captured<Result<BackendUpstream>> ready;
    h.service->ensure_ready(lease, {}, ready.sink());
    h.rt.run_until_idle();
    CHECK(ready.calls == 0);
    h.rt.advance(2s);
    REQUIRE(ready.value);
    CHECK(ready.value->has_value());
    CHECK(h.state().phase == BackendPhase::Running);
    CHECK(h.state().generation == 2);
    CHECK_FALSE(h.state().last_error);
}

TEST_CASE("the restart limit leaves the backend Failed until the next lease", "[backend][service]") {
    ServiceHarness h;
    h.script.child.crash = testing::ScriptedFailure{testing::ScriptStage::Ready, 0ms};
    BackendLease lease = h.lease(1);
    h.rt.advance(2min);
    CHECK(h.state().phase == BackendPhase::Failed);
    REQUIRE(h.state().last_error);
    CHECK(h.state().last_error->id == "backend.restart_limit_reached");
    REQUIRE_FALSE(h.state().last_error->causes.empty());

    Captured<Result<BackendUpstream>> failed;
    h.service->ensure_ready(lease, {}, failed.sink());
    h.rt.run_until_idle();
    REQUIRE(failed.value);
    REQUIRE_FALSE(failed.value->has_value());
    CHECK(failed.value->error().id == "backend.restart_limit_reached");

    lease.release();
    h.script.child.crash.reset();
    BackendLease again = h.lease(2);
    h.rt.run_until_idle();
    CHECK(h.state().phase == BackendPhase::Running);
}

TEST_CASE("a backend that never sends Ready fails with backend.not_ready", "[backend][service]") {
    ServiceHarness h;
    h.script.ready_delay = 5min;
    BackendLease lease = h.lease(1);
    Captured<Result<BackendUpstream>> ready;
    h.service->ensure_ready(lease, {}, ready.sink());
    h.rt.advance(31s);
    REQUIRE(ready.value);
    REQUIRE_FALSE(ready.value->has_value());
    CHECK(ready.value->error().id == "backend.not_ready");
    CHECK(h.state().phase == BackendPhase::Failed);
    REQUIRE(h.state().last_error);
    CHECK(h.state().last_error->id == "backend.not_ready");
    CHECK(h.process->state() != process::ChildState::Running);
}

TEST_CASE("a reconfigure with no lease restarts a pinned backend on the new config", "[backend][service]") {
    ServiceHarness h;
    const Result<OpHandle> start = h.service->start_backend(true, DisconnectPolicy::Detached);
    h.rt.run_until_idle();
    REQUIRE(h.upstream_of(start));
    REQUIRE(h.backend().welcome());
    CHECK(h.backend().welcome()->bind_address == kLoopbackBindAddress);

    const Result<ReconfigureTiming> timing =
        h.service->reconfigure(BackendConfig{BackendTarget{EmbeddedBackend{}}, true}, RunningPolicy::Refuse);
    REQUIRE(timing);
    CHECK(*timing == ReconfigureTiming::Now);
    h.rt.run_until_idle();
    CHECK(h.state().config.allow_lan);
    CHECK(h.state().phase == BackendPhase::Running);
    REQUIRE(h.backends.size() == 2);
    CHECK(h.backend().welcome()->bind_address == kLanBindAddress);
    CHECK(h.events.saw(BackendChange::Reconfigured));

    const Result<ReconfigureTiming> same = h.service->reconfigure(h.state().config, RunningPolicy::Refuse);
    REQUIRE(same);
    CHECK(*same == ReconfigureTiming::Now);
    CHECK(h.backends.size() == 2);
}

TEST_CASE("a reconfigure under a session waits for the last lease", "[backend][service]") {
    ServiceHarness h;
    std::optional<BackendLease> lease = h.lease(1);
    h.rt.run_until_idle();
    const BackendConfig lan{BackendTarget{EmbeddedBackend{}}, true};

    const Result<ReconfigureTiming> refused = h.service->reconfigure(lan, RunningPolicy::Refuse);
    REQUIRE_FALSE(refused);
    CHECK(refused.error().id == "backend.in_use");
    CHECK_FALSE(h.state().pending_config);

    const Result<ReconfigureTiming> waiting = h.service->reconfigure(lan, RunningPolicy::StopSessions);
    REQUIRE(waiting);
    CHECK(*waiting == ReconfigureTiming::AfterLeases);
    CHECK(h.state().pending_config == lan);
    REQUIRE(h.sessions.asked.size() == 1);
    CHECK(h.sessions.asked[0] == std::vector<SessionId>{session_id(1)});

    const Result<BackendLease> blocked = h.service->acquire(session_id(2));
    REQUIRE_FALSE(blocked);
    CHECK(blocked.error().id == "backend.reconfiguring");
    CHECK(blocked.error().retryable);

    lease.reset();
    h.rt.run_until_idle();
    CHECK(h.state().config == lan);
    CHECK_FALSE(h.state().pending_config);
    CHECK(h.state().phase == BackendPhase::Stopped);
    CHECK(h.events.all().back().stop_cause == BackendStopCause::Reconfigure);
}

TEST_CASE("a failed session stop drops the pending reconfigure", "[backend][service]") {
    ServiceHarness h;
    BackendLease lease = h.lease(1);
    h.rt.run_until_idle();
    REQUIRE(h.service->reconfigure(BackendConfig{BackendTarget{EmbeddedBackend{}}, true}, RunningPolicy::StopSessions));
    REQUIRE(h.sessions.pending.size() == 1);
    h.sessions.pending[0](make_diag(ErrorDomain::Sessions, MessageId{"sessions.stop_failed"}).fail());
    CHECK_FALSE(h.state().pending_config);
    REQUIRE(h.state().last_error);
    CHECK(h.state().last_error->id == "sessions.stop_failed");
    CHECK_FALSE(h.state().config.allow_lan);
}

TEST_CASE("a plain-http remote asks once and is then remembered", "[backend][service]") {
    ServiceHarness h(remote_config());
    h.transport.route("GET", "http://play.example:3551/reboot/v1/backend-info", answer(200, std::string(kRebootInfo)));
    const Result<OpHandle> start = h.service->start_backend(false, DisconnectPolicy::Detached);
    h.rt.run_until_idle();
    CHECK(h.state().phase == BackendPhase::Starting);
    const std::optional<UserRequest> asked = pending_request(h.rt.requests(), UserRequestKind::ConfirmUnencryptedUpstream);
    REQUIRE(asked);
    CHECK(asked->op == start->id());
    CHECK(std::any_cast<UnencryptedUpstreamPrompt>(asked->payload).origin == "http://play.example:3551");

    CHECK_FALSE(h.rt.requests().respond(asked->id, std::string("yes")));
    REQUIRE(h.rt.requests().respond(asked->id, UnencryptedUpstreamAnswer{true}));
    h.rt.run_until_idle();
    const std::optional<BackendUpstream> upstream = h.upstream_of(start);
    REQUIRE(upstream);
    CHECK(upstream->origin == "http://play.example:3551");
    CHECK(upstream->flavor == identity::UpstreamFlavor::Reboot);
    CHECK(upstream->websocket == HostPort{"play.example", Port{8443}});
    CHECK_FALSE(upstream->http_port);
    CHECK(h.state().version == "2.0");
    CHECK(h.tls.check(net::UrlScheme::Http, "play.example"));
    CHECK(h.launcher.children().empty());
}

TEST_CASE("declining a plain-http remote fails the start", "[backend][service]") {
    ServiceHarness h(remote_config(net::UrlScheme::Http));
    const Result<OpHandle> start = h.service->start_backend(false, DisconnectPolicy::Detached);
    h.rt.run_until_idle();
    const std::optional<UserRequest> asked = pending_request(h.rt.requests(), UserRequestKind::ConfirmUnencryptedUpstream);
    REQUIRE(asked);
    REQUIRE(h.rt.requests().respond(asked->id, UnencryptedUpstreamAnswer{false}));
    h.rt.run_until_idle();
    const std::optional<ErasedOutcome> failed = h.outcome(start);
    REQUIRE(failed);
    const auto* failure = std::get_if<Failed>(&*failed);
    REQUIRE(failure != nullptr);
    CHECK(failure->error.id == "backend.unencrypted_upstream_declined");
    CHECK(h.state().phase == BackendPhase::Failed);
    CHECK_FALSE(h.tls.check(net::UrlScheme::Http, "play.example"));
}

TEST_CASE("a stop withdraws a pending unencrypted-upstream question", "[backend][service]") {
    ServiceHarness h(remote_config(net::UrlScheme::Http));
    const Result<OpHandle> start = h.service->start_backend(false, DisconnectPolicy::Detached);
    h.rt.run_until_idle();
    REQUIRE(pending_request(h.rt.requests(), UserRequestKind::ConfirmUnencryptedUpstream));
    REQUIRE(h.service->start_stop(DisconnectPolicy::Detached));
    h.rt.run_until_idle();
    CHECK_FALSE(pending_request(h.rt.requests(), UserRequestKind::ConfirmUnencryptedUpstream));
    CHECK(h.state().phase == BackendPhase::Stopped);
}

TEST_CASE("a local backend is probed, never spawned, and is embedded-only for credentials", "[backend][service]") {
    ServiceHarness h(local_config());
    h.transport.route("GET", "http://127.0.0.1:3551/reboot/v1/backend-info", answer(404));
    BackendLease lease = h.lease(1);
    h.rt.run_until_idle();
    CHECK(h.state().phase == BackendPhase::Running);
    REQUIRE(h.state().upstream);
    CHECK(h.state().upstream->flavor == identity::UpstreamFlavor::ThirdParty);
    CHECK(h.state().upstream->websocket == HostPort{"127.0.0.1", storage::kDefaultXmppPort});

    Captured<Result<BackendUpstream>> ready;
    h.service->ensure_ready(lease, {}, ready.sink());
    h.rt.run_until_idle();
    REQUIRE(ready.value);
    REQUIRE(ready.value->has_value());
    CHECK((*ready.value)->origin == "http://127.0.0.1:3551");
    CHECK(h.transport.requests().size() == 2);

    Captured<Result<void>> configured;
    h.service->configure_session(lease, session_config("Player-abc123", "key-1"), configured.sink());
    Captured<Result<LaunchCredential>> minted;
    h.service->mint_launch_credential(lease, exchange_request(),
                                      minted.sink());
    h.rt.run_until_idle();
    REQUIRE(configured.value);
    CHECK(configured.value->has_value());
    REQUIRE(minted.value);
    REQUIRE_FALSE(minted.value->has_value());
    CHECK(minted.value->error().id == "backend.embedded_only");
    const Result<BackendLease> maintenance = h.service->acquire_maintenance();
    REQUIRE_FALSE(maintenance);
    CHECK(maintenance.error().id == "backend.embedded_only");

    lease.release();
    h.rt.run_until_idle();
    CHECK(h.state().phase == BackendPhase::Stopped);
    CHECK(h.launcher.children().empty());
}

TEST_CASE("an unreachable remote fails the start with backend.unreachable", "[backend][service]") {
    ServiceHarness h(remote_config(net::UrlScheme::Https));
    BackendLease lease = h.lease(1);
    Captured<Result<BackendUpstream>> ready;
    h.service->ensure_ready(lease, {}, ready.sink());
    h.rt.run_until_idle();
    REQUIRE(ready.value);
    REQUIRE_FALSE(ready.value->has_value());
    CHECK(ready.value->error().id == "backend.unreachable");
    CHECK(h.state().phase == BackendPhase::Failed);
}

TEST_CASE("the embedded backend mints credentials for a configured lease", "[backend][service]") {
    ServiceHarness h;
    std::vector<LoginObservedEvent> logins;
    h.service->set_login_observer([&](const LoginObservedEvent& event) { logins.push_back(event); });
    h.script.logins = {be::LoginObserved{"Player-abc123", "key-1"}};
    BackendLease lease = h.lease(1);
    Captured<Result<void>> configured;
    h.service->configure_session(lease, session_config("Player-abc123", "key-1"), configured.sink());
    h.rt.run_until_idle();
    REQUIRE(configured.value);
    CHECK(configured.value->has_value());
    REQUIRE(logins.size() == 1);
    CHECK(logins[0].session == session_id(1));

    Captured<Result<LaunchCredential>> minted;
    h.service->mint_launch_credential(lease, exchange_request(),
                                      minted.sink());
    h.rt.run_until_idle();
    REQUIRE(minted.value);
    REQUIRE(minted.value->has_value());
    CHECK((*minted.value)->value.reveal().starts_with("fake-exchange-"));

    lease.release();
    h.rt.run_until_idle();
    // The child has exited and dropped its FakeBackend, so its stdin tells what it was sent.
    const testing::ScriptedChild* child = h.launcher.last(kBackendExe);
    REQUIRE(child != nullptr);
    CHECK(child->stdin_frames().count(contract_frame_type_v<be::EndSession>) == 1);
}

TEST_CASE("waiting for the backend ends on cancel or release", "[backend][service]") {
    ServiceHarness h;
    h.script.ready_delay = 10s;
    BackendLease lease = h.lease(1);
    CancelSource cancel;
    Captured<Result<BackendUpstream>> cancelled;
    h.service->ensure_ready(lease, cancel.token(), cancelled.sink());
    Captured<Result<BackendUpstream>> released;
    h.service->ensure_ready(lease, {}, released.sink());
    h.rt.run_until_idle();
    cancel.cancel(CancelReason::User);
    h.rt.run_until_idle();
    REQUIRE(cancelled.value);
    REQUIRE_FALSE(cancelled.value->has_value());
    CHECK(cancelled.value->error().id == "backend.cancelled");
    CHECK(released.calls == 0);

    lease.release();
    h.rt.run_until_idle();
    REQUIRE(released.value);
    REQUIRE_FALSE(released.value->has_value());
    CHECK(released.value->error().id == "backend.lease_released");

    Captured<Result<BackendUpstream>> late;
    h.service->ensure_ready(lease, {}, late.sink());
    h.rt.run_until_idle();
    REQUIRE(late.value);
    CHECK(late.value->error().id == "backend.lease_released");
}

TEST_CASE("reset and shutdown stops", "[backend][service]") {
    ServiceHarness h;
    std::optional<BackendLease> lease = h.lease(1);
    h.rt.run_until_idle();

    Captured<Result<void>> reset;
    h.service->stop_for(BackendStopCause::Reset, reset.sink());
    h.rt.run_until_idle();
    REQUIRE(reset.value);
    REQUIRE_FALSE(reset.value->has_value());
    CHECK(reset.value->error().id == "backend.in_use");
    CHECK(h.state().phase == BackendPhase::Running);

    Captured<Result<void>> shutdown;
    h.service->stop_for(BackendStopCause::Shutdown, shutdown.sink());
    h.rt.run_until_idle();
    REQUIRE(shutdown.value);
    CHECK(shutdown.value->has_value());
    CHECK(h.state().phase == BackendPhase::Stopped);
    CHECK(h.events.all().back().stop_cause == BackendStopCause::Shutdown);

    CHECK(h.service->acquire(session_id(2)).error().id == "backend.shutting_down");
    CHECK(h.service->start_backend(false, DisconnectPolicy::Detached).error().id == "backend.shutting_down");
    CHECK(h.service->reconfigure(local_config(), RunningPolicy::Refuse).error().id == "backend.shutting_down");
    lease.reset();
    h.rt.run_until_idle();
    CHECK(h.state().phase == BackendPhase::Stopped);
}

TEST_CASE("every op completes when the service goes away", "[backend][service]") {
    ServiceHarness h;
    h.script.ready_delay = 10s;
    const Result<OpHandle> start = h.service->start_backend(false, DisconnectPolicy::Detached);
    h.rt.run_until_idle();
    h.service.reset();
    const std::optional<ErasedOutcome> ended = h.outcome(start);
    REQUIRE(ended);
    CHECK(std::holds_alternative<Cancelled>(*ended));
}

TEST_CASE("a reconfigure away from the embedded backend stops its child", "[backend][service]") {
    ServiceHarness h;
    h.transport.route("GET", "https://play.example:3551/reboot/v1/backend-info", answer(200, std::string(kRebootInfo)));
    const Result<OpHandle> start = h.service->start_backend(true, DisconnectPolicy::Detached);
    h.rt.run_until_idle();
    REQUIRE(h.upstream_of(start));
    REQUIRE(h.process->state() == process::ChildState::Running);

    const Result<ReconfigureTiming> timing = h.service->reconfigure(remote_config(net::UrlScheme::Https), RunningPolicy::Refuse);
    REQUIRE(timing);
    CHECK(*timing == ReconfigureTiming::Now);
    h.rt.run_until_idle();
    CHECK(h.process->state() == process::ChildState::Stopped);
    CHECK(h.state().phase == BackendPhase::Running);
    REQUIRE(h.state().upstream);
    CHECK(h.state().upstream->origin == "https://play.example:3551");
    CHECK(h.launcher.children().size() == 1);
}

TEST_CASE("a stop asked from a ready listener completes", "[backend][service]") {
    ServiceHarness h;
    Captured<Result<void>> stopped;
    bool asked = false;
    h.service->add_ready_listener([&](const BackendState&) {
        if (std::exchange(asked, true)) return;
        h.service->stop_for(BackendStopCause::Reset, stopped.sink());
    });
    REQUIRE(h.service->start_backend(false, DisconnectPolicy::Detached));
    h.rt.run_until_idle();
    REQUIRE(stopped.value);
    CHECK(stopped.value->has_value());
    CHECK(h.state().phase == BackendPhase::Stopped);
    CHECK(h.process->state() == process::ChildState::Stopped);
}
