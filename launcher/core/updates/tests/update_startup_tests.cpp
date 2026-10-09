#include <chrono>
#include <optional>

#include <catch2/catch_test_macros.hpp>

#include "update_service_rig.hpp"

using namespace reboot;
using namespace reboot::updates;
using namespace reboot::updates::test;
using contracts::ipc::ClientKind;
using contracts::ipc::EngineOrigin;

namespace {

PendingUpdateMarker pending(u32 attempts) {
    return PendingUpdateMarker{version(1, 0, 0), version(1, 1, 0), attempts, std::chrono::system_clock::time_point{}};
}

void store_resume(Rig& rig) {
    REQUIRE(rig.resume.update([](storage::ResumeDocument& document) {
        document.origin = EngineOrigin::ServiceManager;
        document.reopen_clients = {ClientKind::WindowsGui};
    }));
}

}  // namespace

TEST_CASE("a start without a marker or resume record has nothing to resume", "[updates][startup]") {
    Rig rig;
    rig.make_service();
    auto startup = rig.service->begin_startup();
    REQUIRE(startup);
    CHECK_FALSE(startup->verdict);
    CHECK_FALSE(startup->marker);
    CHECK_FALSE(startup->resume);
    CHECK(rig.service->confirm_startup({}));
    CHECK(rig.service->state().phase == UpdatePhase::Idle);
}

TEST_CASE("the new version counts its start, self-tests and then confirms", "[updates][startup]") {
    Rig rig({.installed = version(1, 1, 0)});
    rig.write_marker(pending(0));
    store_resume(rig);
    rig.clock.set_system(std::chrono::system_clock::time_point{std::chrono::hours{5}});
    rig.make_service();

    auto startup = rig.service->begin_startup();
    REQUIRE(startup);
    CHECK(startup->verdict == MarkerVerdict::SelfTest);
    REQUIRE(startup->marker);
    CHECK(startup->marker->attempts == 1);
    REQUIRE(startup->resume);
    CHECK(startup->resume->reopen_clients == std::vector{ClientKind::WindowsGui});
    // Counted on disk before the self-test, with the wait restarted.
    auto counted = parse_marker(*rig.fs.contents(rig.marker()));
    REQUIRE(counted);
    CHECK(counted->attempts == 1);
    CHECK(counted->started_at == rig.clock.system_now());
    // Kept until confirmed, so a failed attempt still finds it.
    CHECK(rig.resume.get().reopen_clients == std::vector{ClientKind::WindowsGui});

    REQUIRE(rig.service->confirm_startup({}));
    rig.strand.run_until([&] { return !rig.fs.exists(rig.marker()); });
    CHECK(rig.resume.get().reopen_clients.empty());
    CHECK(rig.service->state().phase == UpdatePhase::Idle);
}

TEST_CASE("a failed self-test keeps the marker; the start past the limit gives up", "[updates][startup]") {
    Rig rig({.installed = version(1, 1, 0)});
    rig.write_marker(pending(0));
    store_resume(rig);

    for (u32 attempt = 1; attempt <= kMaxUpdateAttempts; ++attempt) {
        rig.make_service();
        auto startup = rig.service->begin_startup();
        REQUIRE(startup);
        CHECK(startup->verdict == MarkerVerdict::SelfTest);
        const Result<void> confirmed =
            rig.service->confirm_startup(std::unexpected(make_diag(ErrorDomain::Ipc, MessageId{"ipc.hello_timeout"}).build()));
        REQUIRE_FALSE(confirmed);
        CHECK(confirmed.error().id == "updates.self_test_failed");
        CHECK(confirmed.error().find_arg("attempt") != nullptr);
        CHECK(std::get<u64>(*confirmed.error().find_arg("attempt")) == attempt);
        CHECK(rig.service->state().last_error->id == "updates.self_test_failed");
        CHECK(rig.fs.exists(rig.marker()));
    }

    rig.make_service();
    auto startup = rig.service->begin_startup();
    REQUIRE(startup);
    CHECK(startup->verdict == MarkerVerdict::GiveUp);
    CHECK_FALSE(rig.fs.exists(rig.marker()));
    // The record is still taken once, then cleared.
    REQUIRE(startup->resume);
    CHECK(rig.resume.get().reopen_clients.empty());
    CHECK(rig.service->confirm_startup({}));
    const UpdateState state = rig.service->state();
    CHECK(state.phase == UpdatePhase::Failed);
    REQUIRE(state.last_error);
    CHECK(state.last_error->id == "updates.gave_up");

    rig.service->start();
    const auto failures = rig.published<UpdateFailed>(EventKind::UpdateFailed);
    REQUIRE(failures.size() == 1);
    CHECK(failures[0].stage == UpdateStage::Confirm);
}

TEST_CASE("under the Linux shim the engine does not count its own start", "[updates][startup]") {
    Rig rig({.installed = version(1, 1, 0), .shim_counts_attempts = true});
    rig.write_marker(pending(1));
    rig.make_service();
    auto startup = rig.service->begin_startup();
    REQUIRE(startup);
    CHECK(startup->verdict == MarkerVerdict::SelfTest);
    CHECK(startup->marker->attempts == 1);
    CHECK(parse_marker(*rig.fs.contents(rig.marker()))->attempts == 1);
}

TEST_CASE("the old version after a failed apply reports NotApplied and does not retry on its own", "[updates][startup]") {
    Rig rig({.installed = version(1, 0, 0)});
    rig.write_marker(pending(0));
    store_resume(rig);
    rig.make_service();

    auto startup = rig.service->begin_startup();
    REQUIRE(startup);
    CHECK(startup->verdict == MarkerVerdict::NotApplied);
    REQUIRE(startup->resume);
    CHECK(startup->resume->origin == EngineOrigin::ServiceManager);
    CHECK_FALSE(rig.fs.exists(rig.marker()));
    CHECK(rig.resume.get().reopen_clients.empty());
    UpdateState state = rig.service->state();
    CHECK(state.phase == UpdatePhase::Failed);
    REQUIRE(state.last_error);
    CHECK(state.last_error->id == "updates.not_applied");

    rig.serve_manifest(1, {AppSpec{}});
    rig.service->start();
    auto check = rig.service->start_check(CheckTrigger::User, DisconnectPolicy::BoundToConnection);
    REQUIRE(check);
    rig.finish(*check);
    rig.strand.run_ready();
    state = rig.service->state();
    CHECK(state.phase == UpdatePhase::Available);
    CHECK(rig.ops.live().empty());
    CHECK_FALSE(rig.applier.staged());
    const auto failures = rig.published<UpdateFailed>(EventKind::UpdateFailed);
    REQUIRE(failures.size() == 1);
    CHECK(failures[0].stage == UpdateStage::Apply);
}

TEST_CASE("a malformed or foreign marker is removed", "[updates][startup]") {
    SECTION("malformed") {
        Rig rig;
        rig.fs.write_text(rig.marker(), "{not json");
        rig.make_service();
        auto startup = rig.service->begin_startup();
        REQUIRE(startup);
        CHECK(startup->verdict == MarkerVerdict::Foreign);
        CHECK_FALSE(startup->marker);
        CHECK_FALSE(rig.fs.exists(rig.marker()));
    }
    SECTION("foreign") {
        Rig rig({.installed = version(2, 0, 0)});
        rig.write_marker(pending(0));
        rig.make_service();
        auto startup = rig.service->begin_startup();
        REQUIRE(startup);
        CHECK(startup->verdict == MarkerVerdict::Foreign);
        CHECK_FALSE(rig.fs.exists(rig.marker()));
        CHECK(rig.service->state().phase == UpdatePhase::Idle);
    }
}

TEST_CASE("an unreadable marker fails the startup step", "[updates][startup]") {
    Rig rig;
    rig.write_marker(pending(0));
    rig.fs.faults().fail_next(testing::FsOperation::ReadAll,
                              make_diag(ErrorDomain::Platform, MessageId{"platform.io_failed"}).build());
    rig.make_service();
    CHECK_FALSE(rig.service->begin_startup());
}

TEST_CASE("a resume record without a marker is taken once", "[updates][startup]") {
    Rig rig;
    store_resume(rig);
    rig.make_service();
    auto startup = rig.service->begin_startup();
    REQUIRE(startup);
    CHECK_FALSE(startup->verdict);
    REQUIRE(startup->resume);
    CHECK(startup->resume->origin == EngineOrigin::ServiceManager);
    CHECK(resume_record_from(rig.resume.get()) == ResumeRecord{});
}
