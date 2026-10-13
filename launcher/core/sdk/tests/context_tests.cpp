// The SDK through the real reboot_client against the engine rig on the OS's own endpoint.
#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "reboot/api/v1/engine.hpp"
#include "reboot/api/v1/host.hpp"
#include "reboot/api/v1/method_table.hpp"
#include "reboot/api/v1/secrets.hpp"
#include "reboot/api/v1/sessions.hpp"
#include "reboot/api/v1/settings.hpp"
#include "reboot/base/secret.hpp"
#include "reboot/base/types.hpp"
#include "reboot/sdk/sdk.hpp"
#include "sdk_rig.hpp"

namespace api = rb::api;
using namespace rb::sdk;
using namespace rb::sdk::test;
using namespace std::chrono_literals;

namespace {

[[nodiscard]] api::SecretTarget join_password(const api::HostProfile& profile) {
    api::SecretTarget target;
    target.kind = api::SecretKind::HostJoinPassword;
    target.host_profile = profile.id;
    return target;
}

}  // namespace

TEST_CASE("sdk: connect reads the engine's info, and a client of another schema is refused") {
    SdkRig rig;
    const Context context = rig.connect();
    REQUIRE(context.engine());
    CHECK(context.engine()->schema_fingerprint == RB_SCHEMA_FINGERPRINT);
    CHECK_FALSE(context.bootstrap_only());
    CHECK(context.connection() == ConnectionState::Connected);

    Expected<api::EngineStatusResponse> status = context.engine_status();
    REQUIRE(status);
    CHECK(status->epoch == 77);

    ConnectOptions other = rig.options();
    other.schema_fingerprint = "0000000000000000";
    const Expected<Context> refused = Context::connect(other);
    REQUIRE_FALSE(refused);
    CHECK(refused.error().id() == "sdk.schema_mismatch");
    CHECK(refused.error().english().find("0000000000000000") != std::string::npos);

    other = rig.options();
    other.abi_version = (std::uint32_t{RB_ABI_MAJOR} + 1) << 16;
    const Expected<Context> newer = Context::connect(other);
    REQUIRE_FALSE(newer);
    CHECK(newer.error().status() == RB_E_ABI_MISMATCH);
}

TEST_CASE("sdk: with no engine, connect-only fails with the client's diagnostic") {
    SdkRig rig;
    ConnectOptions options = rig.options(LaunchMode::ConnectOnly);
    options.data_root = rb::display_utf8(rig.rig.root.root / "elsewhere");
    options.connect_deadline = 500ms;
    const Expected<Context> context = Context::connect(options);
    REQUIRE_FALSE(context);
    CHECK(context.error().status() == RB_E_ENGINE_UNAVAILABLE);
    CHECK(context.error().id().starts_with("ipc."));
    CHECK(context.error().english() != context.error().id());
}

TEST_CASE("sdk: calls answer typed responses, and a refusal is the engine's diagnostic in English") {
    SdkRig rig;
    const Context context = rig.connect();

    Expected<api::HostProfilesListResponse> before = context.host_profiles_list();
    REQUIRE(before);
    const api::HostProfile created = SdkRig::profile(context);
    CHECK(created.name == "Arena");
    Expected<api::HostProfilesListResponse> after = context.host_profiles_list();
    REQUIRE(after);
    CHECK(after->profiles.size() == before->profiles.size() + 1);

    REQUIRE(context.host_profiles_delete(api::HostProfilesDeleteRequest{created.id}));
    const Expected<api::HostProfilesDeleteResponse> again = context.host_profiles_delete(api::HostProfilesDeleteRequest{created.id});
    REQUIRE_FALSE(again);
    CHECK(again.error().status() == RB_E_REMOTE);
    CHECK(again.error().id().starts_with("host."));
    const std::string english = again.error().english();
    CHECK_FALSE(english.empty());
    CHECK(english != again.error().id());
    CHECK(english.find('{') == std::string::npos);
}

TEST_CASE("sdk: an operation waiting on the user is cancelled") {
    SdkRig rig;
    const Context context = rig.connect();
    const api::HostProfile profile = SdkRig::profile(context);

    Expected<Op<api::HostStartResponse>> op = context.host_start(api::HostStartRequest{profile.id, {}, std::nullopt});
    REQUIRE(op);
    CHECK(op->method() == api::kHostStart);
    // Still waiting for ConfirmUntested.
    Expected<std::optional<Outcome<api::HostStartResponse>>> running = op->wait(300ms);
    REQUIRE(running);
    CHECK_FALSE(running->has_value());

    REQUIRE(op->cancel());
    Expected<std::optional<Outcome<api::HostStartResponse>>> ended = op->wait(30s);
    REQUIRE(ended);
    REQUIRE(ended->has_value());
    REQUIRE((*ended)->cancelled());
    CHECK((*ended)->cancelled()->reason == api::CancelReason::User);
    Expected<api::HostStartResponse> as_expected = std::move(**ended).into_expected();
    REQUIRE_FALSE(as_expected);
    CHECK(as_expected.error().id() == "sdk.cancelled");
}

TEST_CASE("sdk: an operation completes, and a second client attaches to it and reads the same outcome") {
    SdkRig rig;
    const Context first = rig.connect();
    const Context second = rig.connect();
    const api::HostProfile profile = SdkRig::profile(first);

    Expected<Op<api::HostStartResponse>> op = first.host_start(api::HostStartRequest{profile.id, {}, std::nullopt});
    REQUIRE(op);
    Expected<Op<api::HostStartResponse>> attached = second.attach_host_start(op->id());
    REQUIRE(attached);
    SdkRig::accept(first, api::UserRequestKind::ConfirmUntested);

    Expected<std::optional<Outcome<api::HostStartResponse>>> mine = op->wait(60s);
    REQUIRE(mine);
    REQUIRE(mine->has_value());
    if (const Error* failed = (*mine)->error()) FAIL(failed->id() << ": " << failed->english());
    REQUIRE((*mine)->value());
    Expected<std::optional<Outcome<api::HostStartResponse>>> theirs = attached->wait(60s);
    REQUIRE(theirs);
    REQUIRE(theirs->has_value());
    REQUIRE((*theirs)->value());
    CHECK((*theirs)->value()->session == (*mine)->value()->session);

    const Expected<api::SessionsStopResponse> stopped =
        first.sessions_stop(api::SessionsStopRequest{(*mine)->value()->session, 0}).and_then([](Op<api::SessionsStopResponse> stop) {
            Expected<std::optional<Outcome<api::SessionsStopResponse>>> outcome = stop.wait(60s);
            if (!outcome) return Expected<api::SessionsStopResponse>(std::unexpected(std::move(outcome.error())));
            if (!*outcome) return Expected<api::SessionsStopResponse>(std::unexpected(Error::local(RB_E_TIMEOUT, "sdk.timed_out")));
            return std::move(**outcome).into_expected();
        });
    CHECK(stopped);
}

TEST_CASE("sdk: events reach a handler on the dispatcher through the wake hook") {
    SdkRig rig;
    const Context context = rig.connect();
    Expected<Subscription> events = context.subscribe(api::EventFilter{{api::EventKind::HostProfilesChanged}, std::nullopt, std::nullopt});
    REQUIRE(events);
    QueueDispatcher dispatcher;
    std::vector<Event> seen;
    events->deliver(dispatcher, [&seen](const Event& event) { seen.push_back(event); });

    const api::HostProfile profile = SdkRig::profile(context);
    REQUIRE(dispatcher.run_until([&seen] { return !seen.empty(); }, 30s));
    CHECK(seen.front().kind == api::EventKind::HostProfilesChanged);
    CHECK(seen.front().get<api::HostProfilesChanged>() != nullptr);
    CHECK(events->skipped() == 0);
    CHECK_FALSE(profile.name.empty());
}

TEST_CASE("sdk: a dropped connection is lost, reconnected and resynced, and calls work again") {
    SdkRig rig;
    const Context context = rig.connect();
    Expected<Subscription> events = context.subscribe(api::EventFilter{{api::EventKind::HostProfilesChanged}, std::nullopt, std::nullopt});
    REQUIRE(events);

    rig.drop_connections();
    std::vector<api::EventKind> kinds;
    const auto deadline = std::chrono::steady_clock::now() + 30s;
    while (std::chrono::steady_clock::now() < deadline &&
           std::ranges::find(kinds, api::EventKind::Resync) == kinds.end()) {
        Expected<std::optional<Event>> event = events->next(1s);
        REQUIRE(event);
        if (*event) kinds.push_back((*event)->kind);
    }
    REQUIRE(kinds.size() >= 3);
    CHECK(kinds[0] == api::EventKind::ConnectionLost);
    CHECK(kinds[1] == api::EventKind::Reconnected);
    CHECK(kinds[2] == api::EventKind::Resync);
    CHECK(context.connection() == ConnectionState::Connected);
    CHECK(context.engine_status());
}

TEST_CASE("sdk: a live list of sessions follows a host start and re-snapshots after a lost connection") {
    SdkRig rig;
    const Context context = rig.connect();
    QueueDispatcher dispatcher;
    int changes = 0;
    Expected<LiveList<api::SessionSummary>> sessions =
        LiveList<api::SessionSummary>::open(context, dispatcher, sessions_source(), [&changes] { ++changes; });
    REQUIRE(sessions);
    CHECK(sessions->items().empty());
    CHECK(sessions->snapshots() == 1);

    const api::HostProfile profile = SdkRig::profile(context);
    Expected<Op<api::HostStartResponse>> op = context.host_start(api::HostStartRequest{profile.id, {}, std::nullopt});
    REQUIRE(op);
    SdkRig::accept(context, api::UserRequestKind::ConfirmUntested);
    Expected<std::optional<Outcome<api::HostStartResponse>>> started = op->wait(60s);
    REQUIRE(started);
    REQUIRE(started->has_value());
    REQUIRE((*started)->value());
    const api::SessionId session = (*started)->value()->session;
    REQUIRE(dispatcher.run_until([&] { return sessions->items().size() == 1; }, 30s));
    CHECK(sessions->items().front().id == session);

    rig.drop_connections();
    REQUIRE(dispatcher.run_until([&] { return sessions->snapshots() >= 2 && !sessions->stale(); }, 30s));
    CHECK(sessions->items().size() == 1);
    CHECK(changes > 0);

    Expected<Op<api::SessionsStopResponse>> stop = context.sessions_stop(api::SessionsStopRequest{session, 0});
    REQUIRE(stop);
    REQUIRE(dispatcher.run_until([&] { return sessions->items().empty(); }, 60s));
}

TEST_CASE("sdk: a host join password is stored and read back, and wiped buffers carry it") {
    SdkRig rig;
    const Context context = rig.connect();
    const api::HostProfile profile = SdkRig::profile(context);
    const api::SecretTarget target = join_password(profile);

    REQUIRE(context.put_secret(target, rb::SecretString(std::string("correct horse"))));
    Expected<api::SecretsStateResponse> state = context.secrets_state(api::SecretsStateRequest{target});
    REQUIRE(state);
    CHECK(state->present);
    Expected<rb::SecretString> revealed = context.reveal_secret(target);
    REQUIRE(revealed);
    CHECK(revealed->reveal() == "correct horse");

    api::SecretTarget backend;
    backend.kind = api::SecretKind::BackendPassword;
    backend.backend = api::BackendHost{"example.invalid"};
    const Expected<rb::SecretString> refused = context.reveal_secret(backend);
    REQUIRE_FALSE(refused);
    CHECK(refused.error().status() == RB_E_REMOTE);
}
