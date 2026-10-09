#include <algorithm>
#include <any>
#include <chrono>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "update_service_rig.hpp"
#include "reboot/updates/update_prompts.hpp"

using namespace reboot;
using namespace reboot::updates;
using namespace reboot::updates::test;
using namespace std::chrono_literals;
using contracts::ipc::ClientKind;
using contracts::ipc::EngineOrigin;

namespace {

HostProfileId host_profile(u8 seed) {
    Uuid uuid;
    uuid.bytes[0] = seed;
    return HostProfileId{uuid};
}

ActivitySnapshot playing() {
    ActivitySnapshot snapshot;
    snapshot.live.push_back(LiveActivity{.kind = LiveKind::PlaySession, .payload_version = version(3, 0, 0)});
    return snapshot;
}

ActivitySnapshot hosting(HostProfileId profile) {
    ActivitySnapshot snapshot;
    snapshot.live.push_back(LiveActivity{.kind = LiveKind::HostSession,
                                         .host_profile = profile,
                                         .payload_version = version(3, 1, 0),
                                         .runtime_id = std::string("ge-proton-10")});
    snapshot.connected_clients = {ClientKind::WindowsGui, ClientKind::Cli};
    return snapshot;
}

std::size_t requests_to(const Rig& rig, std::string_view url) {
    const auto all = rig.transport.requests();
    return static_cast<std::size_t>(std::ranges::count_if(all, [&](const ports::HttpRequest& r) { return r.url == url; }));
}

// The service starts its own UpdateApply op after an InPlace offer.
std::optional<OpId> apply_op(const Rig& rig) {
    for (const LiveOp& op : rig.ops.live())
        if (op.kind == OpKind::UpdateApply) return op.op;
    return std::nullopt;
}

std::optional<ErasedOutcome> last_apply_outcome(Rig& rig) {
    std::optional<ErasedOutcome> out;
    for (const OpCompletedEvent& done : rig.published<OpCompletedEvent>(EventKind::OpCompleted))
        if (done.kind == OpKind::UpdateApply) out = done.outcome;
    return out;
}

std::optional<UpdateOffer> check_now(Rig& rig) {
    auto check = rig.service->start_check(CheckTrigger::User, DisconnectPolicy::BoundToConnection);
    REQUIRE(check);
    const ErasedOutcome outcome = rig.finish(*check);
    const auto* offer = completed<std::optional<UpdateOffer>>(outcome);
    REQUIRE(offer != nullptr);
    return *offer;
}

void wait_phase(Rig& rig, UpdatePhase phase) {
    rig.strand.run_until([&] { return rig.service->state().phase == phase; });
}

UserRequest wait_prompt(Rig& rig) {
    rig.strand.run_until([&] { return !rig.requests.pending().empty(); });
    return rig.requests.pending().front();
}

}  // namespace

TEST_CASE("a check without a newer release stays idle and records when it ran", "[updates][check]") {
    Rig rig;
    rig.serve_manifest(1, {AppSpec{.version = version(1, 0, 0)}});
    rig.clock.set_system(std::chrono::system_clock::time_point{std::chrono::hours{100}});
    rig.make_service();

    CHECK_FALSE(check_now(rig));
    const UpdateState state = rig.service->state();
    CHECK(state.phase == UpdatePhase::Idle);
    CHECK(state.mode == UpdateMode::InPlace);
    CHECK(state.last_check == rig.clock.system_now());
    CHECK(rig.state.get().last_update_check == rig.clock.system_now());
    CHECK(rig.published<UpdateAvailable>(EventKind::UpdateAvailable).empty());
}

TEST_CASE("an idle engine downloads, verifies, stages and restarts into the offer", "[updates][apply]") {
    Rig rig;
    rig.activity.set(ActivitySnapshot{.connected_clients = {ClientKind::MacGui, ClientKind::Test}});
    rig.serve_manifest(1, {AppSpec{}});
    rig.serve_package();
    rig.make_service();

    const auto offer = check_now(rig);
    REQUIRE(offer);
    CHECK(offer->entry.version == version(1, 1, 0));
    CHECK_FALSE(offer->required);
    rig.strand.run_until([&] { return static_cast<bool>(rig.pending_apply); });

    const auto staged = rig.applier.staged();
    REQUIRE(staged);
    CHECK(staged->filename() == "RebootLauncher-1.1.0-full.nupkg");
    auto marker = parse_marker(*rig.fs.contents(rig.marker()));
    REQUIRE(marker);
    CHECK(marker->from == version(1, 0, 0));
    CHECK(marker->to == version(1, 1, 0));
    CHECK(marker->attempts == 0);
    CHECK(rig.resume.get().origin == EngineOrigin::ServiceManager);
    CHECK(rig.resume.get().reopen_clients == std::vector{ClientKind::MacGui});

    CHECK(rig.published<UpdateAvailable>(EventKind::UpdateAvailable).size() == 1);
    const auto staged_events = rig.published<UpdateStaged>(EventKind::UpdateStaged);
    REQUIRE(staged_events.size() == 1);
    CHECK(staged_events[0].blocking.empty());
    const auto updating = rig.published<EngineUpdating>(EventKind::EngineUpdating);
    REQUIRE(updating.size() == 1);
    CHECK(updating[0].version == version(1, 1, 0));
    const auto outcome = last_apply_outcome(rig);
    REQUIRE(outcome);
    REQUIRE(completed<ApplyStatus>(*outcome));
    CHECK(*completed<ApplyStatus>(*outcome) == ApplyStatus::Applying);

    CHECK(rig.service->state().phase == UpdatePhase::Applying);
    CHECK(rig.service->admit_new_session().error().id == "updates.busy");
    CHECK(rig.service->start_apply(ApplyWhen::Now).error().id == "updates.busy");
    CHECK(rig.service->start_check(CheckTrigger::User, DisconnectPolicy::Detached).error().id == "updates.busy");
    CHECK(rig.drains == 0);

    REQUIRE(rig.pending_apply());
    CHECK(rig.applier.restarted_with() == resume_args(EngineOrigin::ServiceManager));
}

TEST_CASE("a restart the applier refuses is apply_failed", "[updates][apply]") {
    Rig rig;
    rig.serve_manifest(1, {AppSpec{}});
    rig.serve_package();
    rig.applier.faults().fail_next(testing::UpdateApplierOperation::ApplyAndRestart,
                                   make_diag(ErrorDomain::Platform, MessageId{"platform.velopack_apply_failed"}).build());
    rig.make_service();
    (void)check_now(rig);
    rig.strand.run_until([&] { return static_cast<bool>(rig.pending_apply); });
    const Result<void> applied = rig.pending_apply();
    REQUIRE_FALSE(applied);
    CHECK(applied.error().id == "updates.apply_failed");
    CHECK(applied.error().causes.front().id == "platform.velopack_apply_failed");
    // The engine exits for a restart; the marker lets the old version report NotApplied.
    CHECK(rig.fs.exists(rig.marker()));
}

TEST_CASE("NotifyOnly announces each version once and never downloads", "[updates][notify]") {
    Rig rig({.in_place = false});
    rig.serve_manifest(1, {AppSpec{}});
    rig.make_service();

    REQUIRE(check_now(rig));
    REQUIRE(check_now(rig));
    CHECK(rig.service->state().mode == UpdateMode::NotifyOnly);
    CHECK(rig.service->state().phase == UpdatePhase::Available);
    CHECK(rig.published<UpdateAvailable>(EventKind::UpdateAvailable).size() == 1);
    CHECK(rig.state.get().announced_update == version(1, 1, 0));
    CHECK(rig.service->start_apply(ApplyWhen::WhenIdle).error().id == "updates.notify_only");
    CHECK(rig.service->drain_consented().error().id == "updates.notify_only");
    CHECK(requests_to(rig, kPackageUrl) == 0);
    CHECK_FALSE(apply_op(rig));

    // A restarted engine remembers the announcement.
    rig.make_service();
    REQUIRE(check_now(rig));
    CHECK(rig.published<UpdateAvailable>(EventKind::UpdateAvailable).size() == 1);
}

TEST_CASE("a release below min_supported refuses new sessions only", "[updates][notify]") {
    Rig rig({.in_place = false});
    rig.serve_manifest(1, {AppSpec{.min_supported = version(1, 0, 5)}});
    rig.make_service();
    CHECK(rig.service->admit_new_session());
    const auto offer = check_now(rig);
    REQUIRE(offer);
    CHECK(offer->required);
    const Result<void> admitted = rig.service->admit_new_session();
    REQUIRE_FALSE(admitted);
    CHECK(admitted.error().id == "updates.below_min_supported");
}

TEST_CASE("a failed check is check_failed; only a user check announces it", "[updates][check]") {
    SECTION("user") {
        Rig rig;
        rig.transport.route("GET", std::string(kManifestUrl), testing::FakeHttpResponse{.status = 404});
        rig.make_service();
        auto check = rig.service->start_check(CheckTrigger::User, DisconnectPolicy::BoundToConnection);
        REQUIRE(check);
        const ErasedOutcome outcome = rig.finish(*check);
        REQUIRE(failed(outcome));
        CHECK(failed(outcome)->id == "updates.check_failed");
        CHECK(rig.service->state().phase == UpdatePhase::Failed);
        const auto failures = rig.published<UpdateFailed>(EventKind::UpdateFailed);
        REQUIRE(failures.size() == 1);
        CHECK(failures[0].stage == UpdateStage::Check);
    }
    SECTION("startup") {
        Rig rig({.auto_check = true});
        rig.transport.route("GET", std::string(kManifestUrl), testing::FakeHttpResponse{.status = 404});
        rig.make_service();
        rig.service->start();
        rig.strand.run_until([&] { return rig.service->state().last_error.has_value(); });
        CHECK(rig.service->state().last_error->id == "updates.check_failed");
        CHECK(rig.published<UpdateFailed>(EventKind::UpdateFailed).empty());
    }
}

TEST_CASE("a busy engine stages, waits for the gate and applies once idle", "[updates][gate]") {
    Rig rig;
    rig.activity.set(playing());
    rig.serve_manifest(1, {AppSpec{}});
    rig.serve_package();
    rig.make_service();

    REQUIRE(check_now(rig));
    wait_phase(rig, UpdatePhase::WaitingForIdle);
    rig.strand.run_ready();
    const auto outcome = last_apply_outcome(rig);
    REQUIRE(outcome);
    CHECK(*completed<ApplyStatus>(*outcome) == ApplyStatus::WaitingForIdle);
    const auto staged = rig.published<UpdateStaged>(EventKind::UpdateStaged);
    REQUIRE(staged.size() == 1);
    // The apply op itself never holds the gate.
    CHECK(staged[0].blocking == playing().live);
    CHECK(rig.service->state().blocking == playing().live);
    CHECK(rig.service->admit_new_session());
    CHECK_FALSE(rig.pending_apply);

    // Another apply while waiting completes at once with the same status.
    auto again = rig.service->start_apply(ApplyWhen::WhenIdle);
    REQUIRE(again);
    CHECK(*completed<ApplyStatus>(rig.finish(*again)) == ApplyStatus::WaitingForIdle);
    CHECK(rig.published<UpdateStaged>(EventKind::UpdateStaged).size() == 1);
    CHECK(requests_to(rig, kPackageUrl) == 1);

    rig.activity.set({});
    rig.strand.run_until([&] { return static_cast<bool>(rig.pending_apply); });
    CHECK(rig.drains == 0);
    CHECK(rig.resume.get().relaunch_hosts.empty());
    CHECK(rig.service->state().phase == UpdatePhase::Applying);
}

TEST_CASE("apply now asks first; declining leaves the update waiting", "[updates][prompt]") {
    Rig rig;
    rig.activity.set(playing());
    rig.serve_manifest(1, {AppSpec{}});
    rig.hold_package();
    rig.make_service();

    REQUIRE(check_now(rig));
    const auto running = apply_op(rig);
    REQUIRE(running);
    // The download in progress is returned and now asks once staged.
    auto now = rig.service->start_apply(ApplyWhen::Now);
    REQUIRE(now);
    CHECK(now->id() == *running);
    CHECK(rig.service->state().phase == UpdatePhase::Downloading);
    rig.release_package();

    const UserRequest prompt = wait_prompt(rig);
    CHECK(prompt.kind == UserRequestKind::ConfirmStopSessions);
    CHECK(prompt.op == *running);
    const auto* payload = std::any_cast<ConfirmStopSessionsPrompt>(&prompt.payload);
    REQUIRE(payload != nullptr);
    CHECK(payload->version == version(1, 1, 0));
    CHECK(payload->live == playing().live);
    CHECK(rig.service->state().phase == UpdatePhase::WaitingForIdle);

    // Only the typed answer is accepted.
    CHECK(rig.requests.respond(prompt.id, std::any(42)).error().id == "updates.answer_invalid");
    CHECK(rig.requests.pending().size() == 1);

    REQUIRE(rig.requests.respond(prompt.id, std::any(ConfirmStopSessionsAnswer{.accept = false})));
    const ErasedOutcome outcome = rig.finish(*now);
    REQUIRE(failed(outcome));
    CHECK(failed(outcome)->id == "updates.stop_declined");
    CHECK(rig.drains == 0);
    CHECK(rig.service->state().phase == UpdatePhase::WaitingForIdle);

    rig.activity.set({});
    rig.strand.run_until([&] { return static_cast<bool>(rig.pending_apply); });
}

TEST_CASE("accepting drains, records the drained hosts and applies when the drain ends", "[updates][prompt]") {
    Rig rig({.origin = EngineOrigin::OnDemand});
    const HostProfileId profile = host_profile(7);
    rig.activity.set(hosting(profile));
    rig.serve_manifest(1, {AppSpec{}});
    rig.serve_package();
    rig.make_service();

    REQUIRE(check_now(rig));
    auto now = rig.service->start_apply(ApplyWhen::Now);
    REQUIRE(now);
    const UserRequest prompt = wait_prompt(rig);
    REQUIRE(rig.requests.respond(prompt.id, std::any(ConfirmStopSessionsAnswer{.accept = true})));
    const ErasedOutcome outcome = rig.finish(*now);
    CHECK(*completed<ApplyStatus>(outcome) == ApplyStatus::Draining);
    CHECK(rig.drains == 1);
    CHECK(rig.service->state().phase == UpdatePhase::Draining);
    CHECK(rig.service->drain_consented());
    CHECK(rig.drains == 1);

    // The drain ended the host; the GUI is still connected.
    rig.activity.set(ActivitySnapshot{.connected_clients = {ClientKind::WindowsGui}});
    rig.strand.run_until([&] { return static_cast<bool>(rig.pending_apply); });
    const storage::ResumeDocument& resume = rig.resume.get();
    CHECK(resume.origin == EngineOrigin::OnDemand);
    CHECK(resume.reopen_clients == std::vector{ClientKind::WindowsGui});
    CHECK(resume.relaunch_hosts == std::vector{profile});
    CHECK(resume.payload_version == version(3, 1, 0));
    CHECK(resume.runtime_ids == std::vector<std::string>{"ge-proton-10"});
    REQUIRE(rig.pending_apply());
    CHECK(rig.applier.restarted_with() == resume_args(EngineOrigin::OnDemand));
}

TEST_CASE("a consented drain before staging drains once staged, without asking", "[updates][prompt]") {
    Rig rig;
    rig.activity.set(playing());
    rig.make_service();
    CHECK(rig.service->drain_consented().error().id == "updates.no_update");
    CHECK(rig.service->start_apply(ApplyWhen::Now).error().id == "updates.no_update");

    rig.serve_manifest(1, {AppSpec{}});
    rig.hold_package();
    REQUIRE(check_now(rig));
    REQUIRE(apply_op(rig));
    REQUIRE(rig.service->drain_consented());
    CHECK(rig.drains == 0);
    rig.release_package();
    rig.strand.run_until([&] { return rig.drains == 1; });
    CHECK(rig.requests.pending().empty());
    CHECK(rig.service->state().phase == UpdatePhase::Draining);

    rig.activity.set({});
    rig.strand.run_until([&] { return static_cast<bool>(rig.pending_apply); });
}

TEST_CASE("a client's consent supersedes the engine's own question", "[updates][prompt]") {
    Rig rig;
    rig.activity.set(playing());
    rig.serve_manifest(1, {AppSpec{}});
    rig.serve_package();
    rig.make_service();
    REQUIRE(check_now(rig));
    auto now = rig.service->start_apply(ApplyWhen::Now);
    REQUIRE(now);
    (void)wait_prompt(rig);

    REQUIRE(rig.service->drain_consented());
    CHECK(rig.requests.pending().empty());
    CHECK(rig.drains == 1);
    CHECK(*completed<ApplyStatus>(rig.finish(*now)) == ApplyStatus::Draining);
}

TEST_CASE("cancelling apply now while it asks withdraws the question and keeps the update staged", "[updates][prompt]") {
    Rig rig;
    rig.activity.set(playing());
    rig.serve_manifest(1, {AppSpec{}});
    rig.serve_package();
    rig.make_service();
    REQUIRE(check_now(rig));
    auto now = rig.service->start_apply(ApplyWhen::Now);
    REQUIRE(now);
    const UserRequest prompt = wait_prompt(rig);

    REQUIRE(rig.ops.cancel(now->id(), CancelReason::User));
    rig.strand.run_until([&] { return rig.requests.pending().empty(); });
    CHECK(std::holds_alternative<Cancelled>(*rig.ops.outcome(now->id())));
    CHECK(rig.requests.respond(prompt.id, std::any(ConfirmStopSessionsAnswer{.accept = true})).error().id ==
          "requests.already_resolved");
    CHECK(rig.drains == 0);
    CHECK(rig.service->state().phase == UpdatePhase::WaitingForIdle);

    rig.activity.set({});
    rig.strand.run_until([&] { return static_cast<bool>(rig.pending_apply); });
}

TEST_CASE("sessions ending while the question is open apply at once", "[updates][prompt]") {
    Rig rig;
    rig.activity.set(playing());
    rig.serve_manifest(1, {AppSpec{}});
    rig.serve_package();
    rig.make_service();
    REQUIRE(check_now(rig));
    auto now = rig.service->start_apply(ApplyWhen::Now);
    REQUIRE(now);
    const UserRequest prompt = wait_prompt(rig);

    rig.activity.set({});
    CHECK(*completed<ApplyStatus>(rig.finish(*now)) == ApplyStatus::Applying);
    CHECK(rig.requests.pending().empty());
    rig.strand.run_until([&] { return static_cast<bool>(rig.pending_apply); });
    // A late answer to the withdrawn question changes nothing.
    CHECK_FALSE(rig.requests.respond(prompt.id, std::any(ConfirmStopSessionsAnswer{.accept = true})));
    rig.strand.run_ready();
    CHECK(rig.drains == 0);
}

TEST_CASE("a download that does not match its checksum fails the verify stage", "[updates][apply]") {
    Rig rig;
    rig.serve_manifest(1, {AppSpec{}});
    rig.serve_package(kPackageUrl, "velopack package bytes for 9.9.9");
    rig.make_service();
    REQUIRE(check_now(rig));
    rig.strand.run_until([&] { return rig.service->state().phase == UpdatePhase::Failed; });
    rig.strand.run_ready();

    const auto outcome = last_apply_outcome(rig);
    REQUIRE(outcome);
    REQUIRE(failed(*outcome));
    CHECK(failed(*outcome)->id == "updates.checksum_mismatch");
    const auto failures = rig.published<UpdateFailed>(EventKind::UpdateFailed);
    REQUIRE(failures.size() == 1);
    CHECK(failures[0].stage == UpdateStage::Verify);
    CHECK_FALSE(rig.applier.staged());
    CHECK(rig.service->state().last_error->id == "updates.checksum_mismatch");

    // A later apply downloads again.
    rig.serve_package();
    auto retry = rig.service->start_apply(ApplyWhen::WhenIdle);
    REQUIRE(retry);
    CHECK(*completed<ApplyStatus>(rig.finish(*retry)) == ApplyStatus::Applying);
    CHECK(requests_to(rig, kPackageUrl) == 2);
}

TEST_CASE("a mirror takes over when the first URL fails; all failing is download_failed", "[updates][apply]") {
    SECTION("mirror") {
        Rig rig;
        rig.serve_manifest(1, {AppSpec{.urls = {std::string(kPackageUrl), std::string(kMirrorUrl)}}});
        rig.transport.route("GET", std::string(kPackageUrl), testing::FakeHttpResponse{.status = 404});
        rig.serve_package(kMirrorUrl);
        rig.make_service();
        REQUIRE(check_now(rig));
        rig.strand.run_until([&] { return static_cast<bool>(rig.pending_apply); });
        CHECK(requests_to(rig, kMirrorUrl) == 1);
    }
    SECTION("none") {
        Rig rig;
        rig.serve_manifest(1, {AppSpec{.urls = {std::string(kPackageUrl), std::string(kMirrorUrl)}}});
        rig.transport.route("GET", std::string(kPackageUrl), testing::FakeHttpResponse{.status = 404});
        rig.transport.route("GET", std::string(kMirrorUrl), testing::FakeHttpResponse{.status = 410});
        rig.make_service();
        REQUIRE(check_now(rig));
        wait_phase(rig, UpdatePhase::Failed);
        const Diagnostic error = *rig.service->state().last_error;
        CHECK(error.id == "updates.download_failed");
        CHECK(error.causes.size() == 2);
        const auto failures = rig.published<UpdateFailed>(EventKind::UpdateFailed);
        REQUIRE(failures.size() == 1);
        CHECK(failures[0].stage == UpdateStage::Download);
    }
}

TEST_CASE("a package the applier cannot stage is stage_failed", "[updates][apply]") {
    Rig rig;
    rig.serve_manifest(1, {AppSpec{}});
    rig.serve_package();
    rig.applier.faults().fail_next(testing::UpdateApplierOperation::Stage,
                                   make_diag(ErrorDomain::Platform, MessageId{"platform.velopack_stage_failed"}).build());
    rig.make_service();
    REQUIRE(check_now(rig));
    wait_phase(rig, UpdatePhase::Failed);
    CHECK(rig.service->state().last_error->id == "updates.stage_failed");
    const auto failures = rig.published<UpdateFailed>(EventKind::UpdateFailed);
    REQUIRE(failures.size() == 1);
    CHECK(failures[0].stage == UpdateStage::Stage);
    CHECK(rig.service->admit_new_session());
}

TEST_CASE("cancelling the apply op during the download returns to Available", "[updates][apply]") {
    Rig rig;
    rig.serve_manifest(1, {AppSpec{}});
    rig.transport.route("GET", std::string(kPackageUrl),
                        testing::FakeHttpResponse{.status = 200, .body = bytes_of(kPackage), .stall_after = 4});
    rig.make_service();
    REQUIRE(check_now(rig));
    const auto running = apply_op(rig);
    REQUIRE(running);
    rig.strand.run_until([&] { return rig.transport.in_flight() > 0; });
    REQUIRE(rig.ops.cancel(*running, CancelReason::User));
    rig.strand.run_until([&] { return rig.service->state().phase == UpdatePhase::Available; });
    CHECK(std::holds_alternative<Cancelled>(*rig.ops.outcome(*running)));
    CHECK_FALSE(rig.applier.staged());
    CHECK(rig.published<UpdateFailed>(EventKind::UpdateFailed).empty());
}

TEST_CASE("a resume.json that cannot be written keeps the old version running", "[updates][apply]") {
    Rig rig;
    rig.activity.set(playing());
    rig.serve_manifest(1, {AppSpec{}});
    rig.serve_package();
    rig.make_service();
    REQUIRE(check_now(rig));
    wait_phase(rig, UpdatePhase::WaitingForIdle);

    rig.fs.faults().fail_always(testing::FsOperation::AtomicReplace,
                                make_diag(ErrorDomain::Platform, MessageId{"platform.disk_full"}).build());
    rig.activity.set({});
    wait_phase(rig, UpdatePhase::Failed);
    CHECK(rig.service->state().last_error->id == "updates.apply_failed");
    CHECK_FALSE(rig.pending_apply);
    CHECK_FALSE(rig.fs.exists(rig.marker()));
    CHECK(rig.published<EngineUpdating>(EventKind::EngineUpdating).empty());
    const auto failures = rig.published<UpdateFailed>(EventKind::UpdateFailed);
    REQUIRE(failures.size() == 1);
    CHECK(failures[0].stage == UpdateStage::Apply);
    CHECK(rig.service->admit_new_session());
}

TEST_CASE("the periodic check runs each interval and skips while an apply waits", "[updates][check]") {
    Rig rig({.auto_check = true});
    rig.serve_manifest(1, {AppSpec{.version = version(1, 0, 0)}});
    rig.make_service();
    rig.service->start();
    rig.strand.run_until([&] { return rig.service->state().last_check.has_value(); });
    CHECK(requests_to(rig, kManifestUrl) == 1);

    rig.strand.advance(6h);
    rig.strand.run_until([&] { return requests_to(rig, kManifestUrl) == 2 && rig.ops.live().empty(); });

    rig.activity.set(playing());
    rig.serve_manifest(2, {AppSpec{}});
    rig.serve_package();
    rig.strand.advance(6h);
    wait_phase(rig, UpdatePhase::WaitingForIdle);
    CHECK(requests_to(rig, kManifestUrl) == 3);

    rig.strand.advance(6h);
    CHECK(requests_to(rig, kManifestUrl) == 3);

    rig.service->set_auto_check(false);
    rig.activity.set({});
    rig.strand.run_until([&] { return static_cast<bool>(rig.pending_apply); });
}

TEST_CASE("turning auto-check on checks when the last check is stale, else waits for it", "[updates][check]") {
    Rig rig;
    rig.serve_manifest(1, {AppSpec{.version = version(1, 0, 0)}});
    rig.make_service();
    REQUIRE_FALSE(check_now(rig));
    CHECK(requests_to(rig, kManifestUrl) == 1);

    rig.strand.advance(2h);
    rig.service->set_auto_check(true);
    rig.strand.run_ready();
    CHECK(requests_to(rig, kManifestUrl) == 1);
    rig.strand.advance(4h);
    rig.strand.run_until([&] { return requests_to(rig, kManifestUrl) == 2 && rig.ops.live().empty(); });

    rig.service->set_auto_check(false);
    rig.strand.advance(12h);
    CHECK(requests_to(rig, kManifestUrl) == 2);
}

TEST_CASE("concurrent checks share one op", "[updates][check]") {
    Rig rig;
    rig.serve_manifest(1, {AppSpec{.version = version(1, 0, 0)}});
    rig.make_service();
    auto first = rig.service->start_check(CheckTrigger::User, DisconnectPolicy::BoundToConnection);
    auto second = rig.service->start_check(CheckTrigger::User, DisconnectPolicy::Detached);
    REQUIRE(first);
    REQUIRE(second);
    CHECK(first->id() == second->id());
    CHECK(rig.service->state().phase == UpdatePhase::Checking);
    (void)rig.finish(*first);
    CHECK(requests_to(rig, kManifestUrl) == 1);
}

TEST_CASE("switching channel re-selects from the manifest in use", "[updates][channel]") {
    Rig rig({.in_place = false});
    rig.serve_manifest(1, {AppSpec{.version = version(1, 0, 0)},
                           AppSpec{.channel = "beta", .version = version(1, 2, 0),
                                   .urls = {"https://cdn.test/RebootLauncher-1.2.0-full.nupkg"}}});
    rig.make_service();
    REQUIRE_FALSE(check_now(rig));

    rig.service->set_channel(storage::UpdateChannel::Beta);
    UpdateState state = rig.service->state();
    CHECK(state.channel == storage::UpdateChannel::Beta);
    REQUIRE(state.offer);
    CHECK(state.offer->entry.version == version(1, 2, 0));
    CHECK(state.phase == UpdatePhase::Available);
    CHECK(rig.published<UpdateAvailable>(EventKind::UpdateAvailable).size() == 1);

    rig.service->set_channel(storage::UpdateChannel::Stable);
    state = rig.service->state();
    CHECK_FALSE(state.offer);
    CHECK(state.phase == UpdatePhase::Idle);
    CHECK(requests_to(rig, kManifestUrl) == 1);
}

TEST_CASE("a waiting update keeps its offer across a channel switch", "[updates][channel]") {
    Rig rig;
    rig.activity.set(playing());
    rig.serve_manifest(1, {AppSpec{}});
    rig.serve_package();
    rig.make_service();
    REQUIRE(check_now(rig));
    wait_phase(rig, UpdatePhase::WaitingForIdle);
    rig.service->set_channel(storage::UpdateChannel::Beta);
    CHECK(rig.service->state().phase == UpdatePhase::WaitingForIdle);
    CHECK(rig.service->state().offer->entry.version == version(1, 1, 0));
}

TEST_CASE("the service stops listening to activity when destroyed", "[updates][lifecycle]") {
    Rig rig;
    rig.make_service();
    CHECK(rig.activity.listening());
    rig.service.reset();
    CHECK_FALSE(rig.activity.listening());
}

TEST_CASE("a user who joins a background check hears of its failure", "[updates][check]") {
    Rig rig({.auto_check = true});
    rig.transport.route("GET", std::string(kManifestUrl), testing::FakeHttpResponse{.status = 404});
    rig.make_service();
    rig.service->start();
    REQUIRE(rig.ops.live().size() == 1);
    auto joined = rig.service->start_check(CheckTrigger::User, DisconnectPolicy::BoundToConnection);
    REQUIRE(joined);
    CHECK(joined->id() == rig.ops.live().front().op);
    REQUIRE(failed(rig.finish(*joined)));
    const auto failures = rig.published<UpdateFailed>(EventKind::UpdateFailed);
    REQUIRE(failures.size() == 1);
    CHECK(failures[0].stage == UpdateStage::Check);
}

TEST_CASE("a cancelled apply now during a consented drain keeps the drain consented", "[updates][prompt]") {
    Rig rig;
    rig.activity.set(playing());
    rig.serve_manifest(1, {AppSpec{}});
    rig.serve_package();
    rig.make_service();
    REQUIRE(check_now(rig));
    wait_phase(rig, UpdatePhase::WaitingForIdle);
    REQUIRE(rig.service->drain_consented());
    CHECK(rig.drains == 1);

    auto cancelled = rig.service->start_apply(ApplyWhen::Now);
    REQUIRE(cancelled);
    REQUIRE(rig.ops.cancel(cancelled->id(), CancelReason::User));
    rig.strand.run_ready();
    CHECK(rig.service->state().phase == UpdatePhase::Draining);

    auto now = rig.service->start_apply(ApplyWhen::Now);
    REQUIRE(now);
    CHECK(*completed<ApplyStatus>(rig.finish(*now)) == ApplyStatus::Draining);
    CHECK(rig.requests.pending().empty());
    CHECK(rig.drains == 1);
    CHECK(rig.service->state().phase == UpdatePhase::Draining);
}

TEST_CASE("destroying the service ends its ops and drops their late callbacks", "[updates][lifecycle]") {
    SECTION("download") {
        Rig rig;
        rig.serve_manifest(1, {AppSpec{}});
        rig.hold_package();
        rig.make_service();
        REQUIRE(check_now(rig));
        const auto running = apply_op(rig);
        REQUIRE(running);
        rig.strand.run_until([&] { return rig.transport.in_flight() > 0; });

        rig.service.reset();
        const auto outcome = rig.ops.outcome(*running);
        REQUIRE(outcome);
        REQUIRE(std::holds_alternative<Cancelled>(*outcome));
        CHECK(std::get<Cancelled>(*outcome).reason == CancelReason::Shutdown);
        // The cancelled download still reports back; nothing is left to receive it.
        rig.strand.advance(1s);
        rig.workers.shutdown();
        rig.strand.run_ready();
        CHECK(rig.ops.live().empty());
        CHECK_FALSE(rig.applier.staged());
    }
    SECTION("question") {
        Rig rig;
        rig.activity.set(playing());
        rig.serve_manifest(1, {AppSpec{}});
        rig.serve_package();
        rig.make_service();
        REQUIRE(check_now(rig));
        auto now = rig.service->start_apply(ApplyWhen::Now);
        REQUIRE(now);
        const UserRequest prompt = wait_prompt(rig);

        rig.service.reset();
        CHECK(rig.requests.pending().empty());
        CHECK(std::get<Cancelled>(*rig.ops.outcome(now->id())).reason == CancelReason::Shutdown);
        CHECK_FALSE(rig.requests.respond(prompt.id, std::any(ConfirmStopSessionsAnswer{.accept = true})));
        rig.strand.run_ready();
        CHECK(rig.drains == 0);
    }
    SECTION("check") {
        Rig rig;
        rig.serve_manifest(1, {AppSpec{.version = version(1, 0, 0)}});
        rig.make_service();
        auto check = rig.service->start_check(CheckTrigger::User, DisconnectPolicy::BoundToConnection);
        REQUIRE(check);
        rig.service.reset();
        CHECK(std::get<Cancelled>(*rig.ops.outcome(check->id())).reason == CancelReason::Shutdown);
        rig.workers.shutdown();
        rig.strand.run_ready();
        CHECK_FALSE(rig.state.get().last_update_check);
    }
}
