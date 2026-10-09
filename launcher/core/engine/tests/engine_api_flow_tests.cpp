#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "engine_rig.hpp"
#include "reboot/api/v1/engine.hpp"
#include "reboot/api/v1/guidance.hpp"
#include "reboot/api/v1/host.hpp"
#include "reboot/api/v1/catalog.hpp"
#include "reboot/api/v1/identity.hpp"
#include "reboot/api/v1/integration.hpp"
#include "reboot/api/v1/sessions.hpp"
#include "reboot/api/v1/settings.hpp"
#include "reboot/foundation/types.hpp"
#include "reboot/updates/update_event.hpp"

using namespace reboot;
using namespace reboot::engine;
using namespace reboot::engine::test;
using namespace std::chrono_literals;

namespace {

[[nodiscard]] api::Setting text_setting(std::string key, std::string value) {
    api::Setting setting;
    setting.key = std::move(key);
    setting.value.text = std::move(value);
    return setting;
}

[[nodiscard]] const api::Setting* find_setting(const api::SettingsSnapshotResponse& snapshot, std::string_view key) {
    for (const api::Setting& setting : snapshot.settings)
        if (setting.key == key) return &setting;
    return nullptr;
}

}  // namespace

TEST_CASE("engine flow: a client says hello and reads the engine's status and info") {
    EngineRig rig;
    rig.boot();
    auto client = rig.connect();
    REQUIRE(client->hello_ack());
    CHECK(client->hello_ack()->epoch == 77);

    const Result<api::EngineStatusResponse> status =
        rig.call<api::EngineStatusResponse>(*client, api::kEngineStatus, api::EngineStatusRequest{});
    REQUIRE(status);
    CHECK(status->epoch == 77);
    CHECK(status->pid == 4242);
    CHECK(status->origin == api::EngineOrigin::Foreground);
    CHECK(status->phase == api::EnginePhase::Running);
    CHECK(status->sessions == 0);

    const Result<api::EngineInfoResponse> info = rig.call<api::EngineInfoResponse>(*client, api::kEngineInfo, api::EngineInfoRequest{});
    REQUIRE(info);
    CHECK(info->schema_fingerprint == api::kSchemaFingerprint);
    CHECK(info->storage_mode == api::StorageMode::ReadWrite);

    // runtime.json tells clients which engine owns the data root.
    const std::string runtime = read_file(rig.layout->runtime_file());
    CHECK(runtime.find("4242") != std::string::npos);
}

TEST_CASE("engine flow: a settings patch moves the revision, is announced, and a stale one is refused") {
    EngineRig rig;
    rig.boot();
    auto client = rig.connect();
    const u64 sub = client->subscribe(api::EventFilter{{api::EventKind::SettingsChanged}, std::nullopt, std::nullopt});
    client->credit(sub, 16);

    const Result<api::SettingsSnapshotResponse> before =
        rig.call<api::SettingsSnapshotResponse>(*client, api::kSettingsSnapshot, api::SettingsSnapshotRequest{});
    REQUIRE(before);
    REQUIRE(find_setting(*before, "ui.language") != nullptr);

    api::SettingsPatchRequest patch;
    patch.expected_revision = before->revision;
    patch.changes.push_back(text_setting("ui.language", "de"));
    const Result<api::SettingsPatchResponse> patched = rig.call<api::SettingsPatchResponse>(*client, api::kSettingsPatch, patch);
    REQUIRE(patched);
    CHECK(patched->revision > before->revision);
    REQUIRE(rig.strand.pump_until([&] { return !client->events(sub).empty(); }, 1min));

    const Result<api::SettingsSnapshotResponse> after =
        rig.call<api::SettingsSnapshotResponse>(*client, api::kSettingsSnapshot, api::SettingsSnapshotRequest{});
    REQUIRE(after);
    CHECK(find_setting(*after, "ui.language")->value.text == "de");

    const Result<api::SettingsPatchResponse> stale = rig.call<api::SettingsPatchResponse>(*client, api::kSettingsPatch, patch);
    REQUIRE_FALSE(stale);
    CHECK(stale.error().kind == ErrorKind::Conflict);

    api::SettingsPatchRequest unknown;
    unknown.expected_revision = after->revision;
    unknown.changes.push_back(text_setting("no.such.key", "x"));
    CHECK_FALSE(rig.call<api::SettingsPatchResponse>(*client, api::kSettingsPatch, unknown));
}

TEST_CASE("engine flow: frontend state is stored per shell") {
    EngineRig rig;
    rig.boot();
    auto client = rig.connect();
    api::SettingsFrontendStatePutRequest put;
    put.shell = "cli";
    const std::string_view json = R"({"window":[1,2]})";
    put.state = api::Bytes(json.begin(), json.end());
    REQUIRE(rig.call<api::SettingsFrontendStatePutResponse>(*client, api::kSettingsFrontendStatePut, put));

    // The read is answered from a cache the first call fills, so a retry may be needed.
    std::optional<api::Bytes> state;
    REQUIRE(rig.strand.pump_until([&] {
        Result<api::SettingsFrontendStateGetResponse> got = rig.call<api::SettingsFrontendStateGetResponse>(
            *client, api::kSettingsFrontendStateGet, api::SettingsFrontendStateGetRequest{"cli"});
        if (got) state = got->state;
        return state.has_value();
    }));
    CHECK(*state == put.state);
    put.state = api::Bytes{'{'};
    CHECK_FALSE(rig.call<api::SettingsFrontendStatePutResponse>(*client, api::kSettingsFrontendStatePut, put));

    const Result<api::SettingsFrontendStateGetResponse> unknown = rig.call<api::SettingsFrontendStateGetResponse>(
        *client, api::kSettingsFrontendStateGet, api::SettingsFrontendStateGetRequest{"Not A Shell"});
    CHECK_FALSE(unknown);
}

TEST_CASE("engine flow: host profiles are created, listed and deleted") {
    EngineRig rig;
    rig.boot();
    auto client = rig.connect();
    const Result<api::HostProfilesListResponse> initial =
        rig.call<api::HostProfilesListResponse>(*client, api::kHostProfilesList, api::HostProfilesListRequest{});
    REQUIRE(initial);
    const std::size_t builtin = initial->profiles.size();

    api::HostProfilesCreateRequest create;
    create.profile = EngineRig::arena_profile();
    const Result<api::HostProfilesCreateResponse> created =
        rig.call<api::HostProfilesCreateResponse>(*client, api::kHostProfilesCreate, create);
    REQUIRE(created);
    CHECK(created->profile.name == "Arena");

    const Result<api::HostProfilesListResponse> listed =
        rig.call<api::HostProfilesListResponse>(*client, api::kHostProfilesList, api::HostProfilesListRequest{});
    REQUIRE(listed);
    CHECK(listed->profiles.size() == builtin + 1);

    REQUIRE(rig.call<api::HostProfilesDeleteResponse>(*client, api::kHostProfilesDelete,
                                                      api::HostProfilesDeleteRequest{created->profile.id}));
    CHECK(rig.call<api::HostProfilesListResponse>(*client, api::kHostProfilesList, api::HostProfilesListRequest{})->profiles.size() ==
          builtin);
    CHECK_FALSE(rig.call<api::HostProfilesDeleteResponse>(*client, api::kHostProfilesDelete,
                                                           api::HostProfilesDeleteRequest{created->profile.id}));
}

TEST_CASE("engine flow: identity, guidance and requests answer on a fresh data root") {
    EngineRig rig;
    rig.boot();
    auto client = rig.connect();
    const Result<api::IdentityGetResponse> identity =
        rig.call<api::IdentityGetResponse>(*client, api::kIdentityGet, api::IdentityGetRequest{});
    REQUIRE(identity);
    CHECK_FALSE(identity->profiles.empty());

    const Result<api::GuidanceOnboardingStateResponse> onboarding =
        rig.call<api::GuidanceOnboardingStateResponse>(*client, api::kGuidanceOnboardingState, api::GuidanceOnboardingStateRequest{});
    REQUIRE(onboarding);
    const Result<api::GuidanceOnboardingSkipResponse> skipped =
        rig.call<api::GuidanceOnboardingSkipResponse>(*client, api::kGuidanceOnboardingSkip, api::GuidanceOnboardingSkipRequest{});
    REQUIRE(skipped);
    CHECK(skipped->state != onboarding->state);

    const Result<api::RequestsPendingResponse> pending =
        rig.call<api::RequestsPendingResponse>(*client, api::kRequestsPending, api::RequestsPendingRequest{});
    REQUIRE(pending);
    CHECK(pending->requests.empty());
    api::RequestsRespondRequest stray{99, EngineRig::accept()};
    CHECK_FALSE(rig.call<api::RequestsRespondResponse>(*client, api::kRequestsRespond, stray));

    const Result<api::EngineOperationsResponse> ops =
        rig.call<api::EngineOperationsResponse>(*client, api::kEngineOperations, api::EngineOperationsRequest{});
    REQUIRE(ops);
    CHECK(ops->operations.empty());
}

TEST_CASE("engine flow: a client of another build only gets the bootstrap subset") {
    EngineRig rig;
    rig.boot();
    auto client = rig.connect(EngineRig::default_caller(), "0.0.1-other");
    CHECK(rig.call<api::EngineStatusResponse>(*client, api::kEngineStatus, api::EngineStatusRequest{}));
    CHECK_FALSE(rig.call<api::SettingsSnapshotResponse>(*client, api::kSettingsSnapshot, api::SettingsSnapshotRequest{}));
}

TEST_CASE("engine flow: a user-stop drain refuses new work and ends the engine") {
    EngineRig rig;
    rig.boot();
    auto client = rig.connect();
    const Result<api::HostProfilesCreateResponse> profile = rig.call<api::HostProfilesCreateResponse>(
        *client, api::kHostProfilesCreate, api::HostProfilesCreateRequest{EngineRig::arena_profile()});
    REQUIRE(profile);
    const u64 drain = client->call(api::kEngineDrain, api::EngineDrainRequest{api::DrainReason::UserStop});
    api::HostStartRequest start;
    start.profile = profile->profile.id;
    const u64 late = client->start(api::kHostStart, start);
    REQUIRE(rig.strand.pump_until([&] { return rig.exit.has_value(); }, 5min));
    CHECK(*rig.exit == EngineExit::Ok);
    rig.settle();
    REQUIRE(client->response<api::EngineDrainResponse>(drain));
    CHECK(*client->response<api::EngineDrainResponse>(drain));
    CHECK_FALSE(client->started_op(late));
    REQUIRE(client->reply_payload(late));
    CHECK_FALSE(*client->reply_payload(late));
}

TEST_CASE("engine flow: shutdown now exits and says goodbye") {
    EngineRig rig;
    rig.boot();
    auto client = rig.connect();
    REQUIRE(rig.call<api::EngineShutdownResponse>(*client, api::kEngineShutdown, api::EngineShutdownRequest{api::ShutdownWhen::Now}));
    REQUIRE(rig.strand.pump_until([&] { return rig.exit.has_value(); }, 5min));
    CHECK(*rig.exit == EngineExit::Ok);
    rig.settle();
    CHECK(client->goodbye_received().has_value());
}

TEST_CASE("engine flow: restart_when_idle exits with the restart code and records the origin") {
    EngineRig rig;
    rig.boot(EngineOrigin::OnDemand);
    auto client = rig.connect();
    REQUIRE(rig.call<api::EngineRestartWhenIdleResponse>(*client, api::kEngineRestartWhenIdle, api::EngineRestartWhenIdleRequest{}));
    REQUIRE(rig.strand.pump_until([&] { return rig.exit.has_value(); }, 5min));
    CHECK(*rig.exit == EngineExit::Restart);
    CHECK(std::filesystem::exists(rig.layout->resume_file()));
    rig.settle();
    CHECK(client->goodbye_received() == contracts::ipc::GoodbyeReason::Restarting);
}

TEST_CASE("engine flow: an update drain refuses new work but not stops, and a failed update ends it") {
    EngineRig rig;
    rig.boot();
    auto client = rig.connect();
    rig.services->lifecycle().drain_for_update();
    CHECK(rig.call<api::EngineStatusResponse>(*client, api::kEngineStatus, api::EngineStatusRequest{})->phase ==
          api::EnginePhase::Draining);

    const Result<u64> refused = rig.start(*client, api::kCatalogRefresh, api::CatalogRefreshRequest{});
    REQUIRE_FALSE(refused);
    CHECK(refused.error().id == "engine.shutting_down");
    // A stop is let through to the sessions, which know no such session.
    const Result<u64> stop = rig.start(*client, api::kSessionsStop, api::SessionsStopRequest{api::SessionId{Uuid{}}, 0});
    REQUIRE_FALSE(stop);
    CHECK(stop.error().id != "engine.shutting_down");

    rig.services->events().publish(
        EventKind::UpdateFailed,
        updates::UpdateFailed{updates::UpdateStage::Apply, make_diag(ErrorDomain::Engine, MessageId{"engine.test_failure"}).build()});
    CHECK(rig.call<api::EngineStatusResponse>(*client, api::kEngineStatus, api::EngineStatusRequest{})->phase ==
          api::EnginePhase::Running);
    CHECK(rig.start(*client, api::kCatalogRefresh, api::CatalogRefreshRequest{}));
    CHECK(rig.call<api::EngineDrainResponse>(*client, api::kEngineDrain, api::EngineDrainRequest{api::DrainReason::Replace})
              .error()
              .id != "engine.already_draining");
}

TEST_CASE("engine flow: requests with values this build does not know are refused") {
    EngineRig rig;
    rig.boot();
    auto client = rig.connect();
    api::IntegrationPurgeRequest purge;
    purge.scope = static_cast<api::PurgeScope>(7);
    purge.running_policy = api::RunningPolicy::StopSessions;
    const Result<u64> purged = rig.start(*client, api::kIntegrationPurge, purge);
    REQUIRE_FALSE(purged);
    CHECK(purged.error().id == "engine.invalid_request");

    api::IntegrationApplyRequest apply;
    apply.items = {api::IntegrationItem::UrlScheme, static_cast<api::IntegrationItem>(9)};
    const Result<u64> applied = rig.start(*client, api::kIntegrationApply, apply);
    REQUIRE_FALSE(applied);
    CHECK(applied.error().id == "engine.invalid_request");
}

TEST_CASE("engine flow: an on-demand engine with no client and no work exits by itself") {
    EngineRig rig;
    rig.boot(EngineOrigin::OnDemand);
    {
        auto client = rig.connect();
        client->goodbye();
        client->close();
    }
    REQUIRE(rig.strand.pump_until([&] { return rig.exit.has_value(); }, 30min));
    CHECK(*rig.exit == EngineExit::Ok);
}

TEST_CASE("engine flow: the idle exit waits for a detached op that started without an event") {
    EngineRig rig;
    rig.boot(EngineOrigin::OnDemand);
    {
        auto client = rig.connect();
        client->goodbye();
        client->close();
    }
    rig.settle();
    auto [handle, op] = rig.services->ops().create<void>(OpKind::Generic, DisconnectPolicy::Detached, std::nullopt,
                                                         RunnerMultiplier::Native, std::chrono::hours{1});
    CHECK_FALSE(rig.strand.pump_until([&] { return rig.exit.has_value(); }, 6min, 3s));
    op.complete(Completed<void>{});
    REQUIRE(rig.strand.pump_until([&] { return rig.exit.has_value(); }, 30min));
    CHECK(*rig.exit == EngineExit::Ok);
    static_cast<void>(handle);
}
