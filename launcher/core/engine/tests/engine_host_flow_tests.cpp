#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "api_convert.hpp"
#include "engine_rig.hpp"
#include "reboot/api/v1/engine.hpp"
#include "reboot/api/v1/host.hpp"
#include "reboot/api/v1/sessions.hpp"
#include "reboot/api/v1/settings.hpp"

using namespace reboot;
using namespace reboot::engine;
using namespace reboot::engine::test;
using namespace std::chrono_literals;

namespace {

struct HostRig {
    HostRig() {
        rig.serve_backend();
        rig.serve_game_server();
        rig.boot();
        client = rig.connect();
        rig.auto_accept(*client, {api::UserRequestKind::ConfirmUntested});
        Result<api::HostProfilesCreateResponse> created = rig.call<api::HostProfilesCreateResponse>(
            *client, api::kHostProfilesCreate, api::HostProfilesCreateRequest{EngineRig::arena_profile()});
        REQUIRE(created);
        profile = created->profile;
    }

    [[nodiscard]] api::SessionId start() {
        api::HostStartRequest request;
        request.profile = profile.id;
        Result<u64> op = rig.start(*client, api::kHostStart, request);
        REQUIRE(op);
        return EngineRig::completed<api::HostStartResponse>(rig.settle_op(*client, *op)).session;
    }

    [[nodiscard]] std::optional<api::SessionSummary> summary(const api::SessionId& session) {
        Result<api::SessionsListResponse> listed = rig.call<api::SessionsListResponse>(*client, api::kSessionsList, api::SessionsListRequest{});
        REQUIRE(listed);
        for (const api::SessionSummary& entry : listed->sessions)
            if (entry.id == session) return entry;
        return std::nullopt;
    }

    EngineRig rig;
    std::unique_ptr<testing::ApiTestClient> client;
    api::HostProfile profile;
};

}  // namespace

TEST_CASE("host flow: a profile starts our game server behind the backend, then stops") {
    HostRig h;
    const api::SessionId session = h.start();
    CHECK(h.rig.backends_spawned == 1);
    REQUIRE_FALSE(h.rig.game_servers.empty());
    REQUIRE(h.rig.game_servers.back()->config());

    const Result<api::HostStatusResponse> status =
        h.rig.call<api::HostStatusResponse>(*h.client, api::kHostStatus, api::HostStatusRequest{session});
    REQUIRE(status);
    REQUIRE(status->status.listening);
    CHECK_FALSE(status->status.listening->sockets.empty());

    const std::optional<api::SessionSummary> running = h.summary(session);
    REQUIRE(running);
    CHECK(running->kind == api::SessionKind::Host);
    CHECK(h.rig.call<api::EngineStatusResponse>(*h.client, api::kEngineStatus, api::EngineStatusRequest{})->sessions == 1);

    Result<u64> stop = h.rig.start(*h.client, api::kSessionsStop, api::SessionsStopRequest{session, 0});
    REQUIRE(stop);
    static_cast<void>(EngineRig::completed<api::SessionsStopResponse>(h.rig.settle_op(*h.client, *stop)));
    REQUIRE(h.rig.strand.pump_until([&] {
        const std::optional<api::SessionSummary> now = h.summary(session);
        return !now || now->phase == api::SessionPhase::Ended;
    }, 5min));
    CHECK(h.rig.call<api::EngineStatusResponse>(*h.client, api::kEngineStatus, api::EngineStatusRequest{})->sessions == 0);
}

TEST_CASE("host flow: an operator command reaches the running server") {
    HostRig h;
    const api::SessionId session = h.start();
    api::HostCommandRequest command;
    command.session = session;
    command.command.console = api::ConsoleCommand{"say hello"};
    REQUIRE(h.rig.call<api::HostCommandResponse>(*h.client, api::kHostCommand, command));
    REQUIRE(h.rig.strand.pump_until([&] { return !h.rig.game_servers.back()->commands().empty(); }, 1min));
    CHECK(h.rig.game_servers.back()->commands().front() == "say hello");
}

TEST_CASE("host flow: a hosting profile cannot be deleted or have its identity replaced") {
    HostRig h;
    const api::SessionId session = h.start();
    CHECK_FALSE(h.rig.call<api::HostProfilesDeleteResponse>(*h.client, api::kHostProfilesDelete,
                                                            api::HostProfilesDeleteRequest{h.profile.id}));
    api::HostIdentityImportRequest import_request;
    import_request.profile = h.profile.id;
    const NativePath source = h.rig.scratch->path() / "some-export";
    std::filesystem::create_directories(source);
    import_request.source = convert::path(source);
    const Result<u64> op = h.rig.start(*h.client, api::kHostIdentityImport, import_request);
    REQUIRE_FALSE(op);
    CHECK(op.error().id == "host.profile_busy");
    static_cast<void>(session);
}

TEST_CASE("host flow: identity export needs a registered identity, and import an export directory") {
    HostRig h;
    api::HostIdentityExportRequest export_request;
    export_request.profile = h.profile.id;
    export_request.destination = convert::path(h.rig.scratch->path() / "exported-identity");
    const Result<u64> exporting = h.rig.start(*h.client, api::kHostIdentityExport, export_request);
    REQUIRE_FALSE(exporting);
    CHECK(exporting.error().id == "publish.identity_not_registered");

    const NativePath bogus = h.rig.scratch->path() / "not-an-export";
    std::filesystem::create_directories(bogus);
    api::HostIdentityImportRequest import_request;
    import_request.profile = h.profile.id;
    import_request.source = convert::path(bogus);
    const Result<u64> importing = h.rig.start(*h.client, api::kHostIdentityImport, import_request);
    REQUIRE(importing);
    const api::Outcome outcome = h.rig.settle_op(*h.client, *importing);
    REQUIRE(outcome.failed);
    CHECK(outcome.failed->id == "publish.identity_file_invalid");

    import_request.source = api::Path{"relative", {}};
    CHECK_FALSE(h.rig.start(*h.client, api::kHostIdentityImport, import_request));
}
