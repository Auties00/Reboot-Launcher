#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "integration_test_support.hpp"
#include "reboot/foundation/cancel.hpp"
#include "reboot/foundation/executor.hpp"
#include "reboot/integration/integration_changed.hpp"
#include "reboot/integration/integration_service.hpp"
#include "reboot/storage/document_store.hpp"
#include "reboot/storage/state_document.hpp"
#include "reboot/testing/event_recorder.hpp"
#include "reboot/testing/fake_platform_paths.hpp"
#include "reboot/testing/fake_registrar.hpp"
#include "reboot/testing/in_memory_file_system.hpp"

using namespace reboot;
using namespace reboot::integration;
using ports::IntegrationState;
using testing::RegistrarOperation;

namespace {

const std::string kSchemeCommand = R"("C:/Reboot/Reboot.exe" --activate-url "%1")";
const std::string kAutostartCommand = R"("C:/Reboot/reboot-engine.exe" run --origin=service-manager)";

[[nodiscard]] IntegrationTargets windows_targets() {
    return IntegrationTargets{.flavor = EntryFlavor::Windows,
                              .gui_exe = NativePath("C:/Reboot/Reboot.exe"),
                              .engine_exe = NativePath("C:/Reboot/reboot-engine.exe")};
}

[[nodiscard]] const EntryStatus& of(const std::vector<EntryStatus>& statuses, IntegrationKind kind) {
    const auto found = std::ranges::find(statuses, kind, &EntryStatus::kind);
    REQUIRE(found != statuses.end());
    return *found;
}

[[nodiscard]] SemVer version(u32 major, u32 minor = 0) { return SemVer{.major = major, .minor = minor}; }

struct Fixture {
    Fixture() {
        static_cast<void>(state.load_memory_only(test::fault("storage.memory_only")));
        registrar.set_detail(IntegrationKind::UrlScheme, kSchemeCommand);
        registrar.set_detail(IntegrationKind::Autostart, kAutostartCommand);
    }

    void set_state(UniqueFunction<void(storage::StateDocument&)> mutate) { REQUIRE(state.update(std::move(mutate))); }

    [[nodiscard]] std::vector<EntryStatus> run(Result<OpHandle> started) {
        REQUIRE(started);
        return test::completed_value<std::vector<EntryStatus>>(rig.wait(started->id()));
    }

    [[nodiscard]] std::optional<ReconcileReport> reconcile(SemVer running, CancelToken token = {}) {
        bool called = false;
        std::optional<ReconcileReport> report;
        service.reconcile(std::move(running), std::move(token), [&](std::optional<ReconcileReport> done) {
            called = true;
            report = std::move(done);
        });
        rig.strand.run_until([&] { return called; });
        return report;
    }

    [[nodiscard]] std::vector<IntegrationChanged> changes() {
        recorder.pump();
        std::vector<IntegrationChanged> out;
        for (const IntegrationChanged* change : recorder.payloads<IntegrationChanged>(EventKind::IntegrationChanged))
            out.push_back(*change);
        recorder.clear();
        return out;
    }

    test::OpRig rig;
    WorkerPool workers{1};
    testing::InMemoryFileSystem fs;
    storage::DocumentStore<storage::StateDocument> state{fs, workers, rig.strand, rig.clock,
                                                          testing::default_fake_root() / "state.json"};
    testing::FakeRegistrar registrar;
    testing::EventRecorder recorder{rig.events, EventFilter{.kinds = {EventKind::IntegrationChanged}}};
    IntegrationService service{registrar, state, workers, rig.strand, rig.ops, rig.events, windows_targets()};
};

}  // namespace

TEST_CASE("status reads every kind from the entries, with the declined flags", "[integration][service]") {
    Fixture f;
    f.set_state([](storage::StateDocument& document) { document.declined_integrations = {IntegrationKind::Autostart}; });
    REQUIRE(f.registrar.apply(IntegrationKind::UrlScheme, NativePath("C:/Reboot/Reboot.exe")));

    std::optional<std::vector<EntryStatus>> read;
    f.service.status({}, [&](std::vector<EntryStatus> entries) { read = std::move(entries); });
    CHECK_FALSE(read);
    f.rig.strand.run_until([&] { return read.has_value(); });

    REQUIRE(read->size() == kAllIntegrationKinds.size());
    CHECK(of(*read, IntegrationKind::UrlScheme).state == EntryState::Ours);
    CHECK(of(*read, IntegrationKind::UrlScheme).target == kSchemeCommand);
    CHECK(of(*read, IntegrationKind::Autostart).state == EntryState::Absent);
    CHECK(of(*read, IntegrationKind::Autostart).declined);
    CHECK(of(*read, IntegrationKind::EngineAgent).state == EntryState::Absent);
    CHECK(of(*read, IntegrationKind::DesktopEntry).state == EntryState::Unsupported);
    CHECK(f.changes().empty());
}

TEST_CASE("a status read that fails is Unknown with the reason", "[integration][service]") {
    Fixture f;
    f.registrar.faults().fail_next(RegistrarOperation::Status, test::fault("platform.registry_failed"));
    std::optional<std::vector<EntryStatus>> read;
    f.service.status({}, [&](std::vector<EntryStatus> entries) { read = std::move(entries); });
    f.rig.strand.run_until([&] { return read.has_value(); });
    const EntryStatus& scheme = of(*read, IntegrationKind::UrlScheme);
    CHECK(scheme.state == EntryState::Unknown);
    REQUIRE(scheme.detail);
    CHECK(scheme.detail->id == "integration.inspect_failed");
}

TEST_CASE("apply writes absent entries, clears declined flags and publishes every entry", "[integration][service]") {
    Fixture f;
    f.set_state([](storage::StateDocument& document) { document.declined_integrations = {IntegrationKind::UrlScheme}; });

    const std::vector<EntryStatus> applied = f.run(f.service.start_apply(
        {IntegrationKind::UrlScheme, IntegrationKind::UrlScheme, IntegrationKind::EngineAgent}, DisconnectPolicy::Detached));
    REQUIRE(applied.size() == 2);
    CHECK(applied[0].kind == IntegrationKind::UrlScheme);
    CHECK(applied[0].state == EntryState::Ours);
    CHECK_FALSE(applied[0].declined);
    CHECK_FALSE(applied[0].detail);
    CHECK(applied[1].kind == IntegrationKind::EngineAgent);
    CHECK(applied[1].state == EntryState::Ours);
    CHECK(f.registrar.applied_exe(IntegrationKind::UrlScheme) == NativePath("C:/Reboot/Reboot.exe"));
    CHECK(f.registrar.applied_exe(IntegrationKind::EngineAgent) == NativePath("C:/Reboot/reboot-engine.exe"));
    CHECK(f.state.get().declined_integrations.empty());

    const std::vector<IntegrationChanged> changes = f.changes();
    REQUIRE(changes.size() == 1);
    REQUIRE(changes[0].entries.size() == kAllIntegrationKinds.size());
    CHECK(of(changes[0].entries, IntegrationKind::UrlScheme).state == EntryState::Ours);
    CHECK(of(changes[0].entries, IntegrationKind::Autostart).state == EntryState::Absent);
}

TEST_CASE("apply never touches a foreign entry or one the user turned off", "[integration][service]") {
    Fixture f;
    f.registrar.set_state(IntegrationKind::UrlScheme, IntegrationState::Foreign);
    f.registrar.set_detail(IntegrationKind::UrlScheme, R"("D:/Other/Other.exe" "%1")");
    f.registrar.set_state(IntegrationKind::Autostart, IntegrationState::Ours);
    f.registrar.set_detail(IntegrationKind::Autostart, "disabled");

    const std::vector<EntryStatus> applied = f.run(
        f.service.start_apply({IntegrationKind::UrlScheme, IntegrationKind::Autostart}, DisconnectPolicy::Detached));
    const EntryStatus& scheme = of(applied, IntegrationKind::UrlScheme);
    CHECK(scheme.state == EntryState::Foreign);
    CHECK(scheme.target == R"("D:/Other/Other.exe" "%1")");
    REQUIRE(scheme.detail);
    CHECK(scheme.detail->id == "integration.foreign_entry");
    CHECK(of(applied, IntegrationKind::Autostart).state == EntryState::Disabled);
    CHECK_FALSE(of(applied, IntegrationKind::Autostart).detail);
    CHECK_FALSE(f.registrar.applied_exe(IntegrationKind::UrlScheme));
    CHECK_FALSE(f.registrar.applied_exe(IntegrationKind::Autostart));
}

TEST_CASE("an entry still not pointing at us after the write is NotApplied", "[integration][service]") {
    Fixture f;
    f.registrar.set_detail(IntegrationKind::UrlScheme, R"("C:/Reboot/Reboot.exe")");
    const std::vector<EntryStatus> applied =
        f.run(f.service.start_apply({IntegrationKind::UrlScheme}, DisconnectPolicy::Detached));
    CHECK(applied[0].state == EntryState::Stale);
    REQUIRE(applied[0].detail);
    CHECK(applied[0].detail->id == "integration.not_applied");
}

TEST_CASE("a failed write is the entry's detail, not the op's failure", "[integration][service]") {
    Fixture f;
    f.registrar.faults().fail_next(RegistrarOperation::Apply, test::fault("platform.access_denied"));
    const std::vector<EntryStatus> applied = f.run(
        f.service.start_apply({IntegrationKind::EngineAgent, IntegrationKind::UrlScheme}, DisconnectPolicy::Detached));
    REQUIRE(of(applied, IntegrationKind::EngineAgent).detail);
    CHECK(of(applied, IntegrationKind::EngineAgent).detail->id == "integration.write_failed");
    CHECK(of(applied, IntegrationKind::EngineAgent).state == EntryState::Absent);
    CHECK(of(applied, IntegrationKind::UrlScheme).state == EntryState::Ours);
}

TEST_CASE("apply and remove refuse an empty list without an op", "[integration][service]") {
    Fixture f;
    for (Result<OpHandle> started : {f.service.start_apply({}, DisconnectPolicy::Detached),
                                     f.service.start_remove({}, DisconnectPolicy::Detached)}) {
        REQUIRE_FALSE(started);
        CHECK(started.error().id == "integration.no_items");
        CHECK(started.error().kind == ErrorKind::InvalidInput);
    }
    CHECK(f.rig.ops.live().empty());
}

TEST_CASE("remove takes only our entries and declines every named kind", "[integration][service]") {
    Fixture f;
    REQUIRE(f.registrar.apply(IntegrationKind::UrlScheme, NativePath("C:/Reboot/Reboot.exe")));
    REQUIRE(f.registrar.apply(IntegrationKind::Autostart, NativePath("C:/Reboot/reboot-engine.exe")));
    f.registrar.set_state(IntegrationKind::EngineAgent, IntegrationState::Foreign);

    const std::vector<EntryStatus> removed = f.run(
        f.service.start_remove({IntegrationKind::UrlScheme, IntegrationKind::EngineAgent}, DisconnectPolicy::Detached));
    CHECK(of(removed, IntegrationKind::UrlScheme).state == EntryState::Absent);
    CHECK(of(removed, IntegrationKind::UrlScheme).declined);
    CHECK_FALSE(of(removed, IntegrationKind::UrlScheme).detail);
    CHECK(of(removed, IntegrationKind::EngineAgent).state == EntryState::Foreign);
    REQUIRE(of(removed, IntegrationKind::EngineAgent).detail);
    CHECK(of(removed, IntegrationKind::EngineAgent).detail->id == "integration.foreign_entry");
    CHECK(f.registrar.status(IntegrationKind::EngineAgent)->state == IntegrationState::Foreign);
    CHECK(f.registrar.status(IntegrationKind::Autostart)->state == IntegrationState::Ours);
    CHECK(f.state.get().declined_integrations ==
          std::vector{IntegrationKind::UrlScheme, IntegrationKind::EngineAgent});

    const std::vector<IntegrationChanged> changes = f.changes();
    REQUIRE(changes.size() == 1);
    CHECK(of(changes[0].entries, IntegrationKind::UrlScheme).declined);
    CHECK_FALSE(of(changes[0].entries, IntegrationKind::Autostart).declined);

    // Applying again takes the decline back.
    static_cast<void>(f.run(f.service.start_apply({IntegrationKind::UrlScheme}, DisconnectPolicy::Detached)));
    CHECK(f.state.get().declined_integrations == std::vector{IntegrationKind::EngineAgent});
}

TEST_CASE("changes run one at a time and one cancelled while queued writes nothing", "[integration][service]") {
    Fixture f;
    const Result<OpHandle> first = f.service.start_apply({IntegrationKind::UrlScheme}, DisconnectPolicy::Detached);
    const Result<OpHandle> second = f.service.start_apply({IntegrationKind::Autostart}, DisconnectPolicy::Detached);
    const Result<OpHandle> third = f.service.start_remove({IntegrationKind::UrlScheme}, DisconnectPolicy::Detached);
    REQUIRE(first);
    REQUIRE(second);
    REQUIRE(third);
    REQUIRE(f.rig.ops.cancel(second->id(), CancelReason::User));

    CHECK(of(test::completed_value<std::vector<EntryStatus>>(f.rig.wait(first->id())), IntegrationKind::UrlScheme)
              .state == EntryState::Ours);
    // Each change is published; pumped now, since the next one would coalesce with it.
    CHECK(f.changes().size() == 1);
    CHECK(std::holds_alternative<Cancelled>(f.rig.wait(second->id())));
    // The remove ran after the apply it was queued behind.
    CHECK(of(test::completed_value<std::vector<EntryStatus>>(f.rig.wait(third->id())), IntegrationKind::UrlScheme)
              .state == EntryState::Absent);
    CHECK_FALSE(f.registrar.applied_exe(IntegrationKind::Autostart));
    CHECK(f.state.get().declined_integrations == std::vector{IntegrationKind::UrlScheme});
    CHECK(f.changes().size() == 1);
}

TEST_CASE("the first start writes every entry but Autostart and records the version", "[integration][reconcile]") {
    Fixture f;
    const std::optional<ReconcileReport> report = f.reconcile(version(1));
    REQUIRE(report);
    CHECK_FALSE(report->previous);
    CHECK(report->running == version(1));
    CHECK(report->written == std::vector{IntegrationKind::UrlScheme, IntegrationKind::EngineAgent});
    REQUIRE(report->items.size() == kAllIntegrationKinds.size());
    CHECK(of(report->items, IntegrationKind::Autostart).state == EntryState::Absent);
    CHECK(of(report->items, IntegrationKind::DesktopEntry).state == EntryState::Unsupported);
    CHECK(f.state.get().last_run_version == version(1));
    CHECK(f.changes().size() == 1);

    CHECK_FALSE(f.reconcile(version(1)));
    CHECK(f.changes().empty());
}

TEST_CASE("a new version rewrites stale entries and leaves declined ones absent", "[integration][reconcile]") {
    Fixture f;
    f.set_state([](storage::StateDocument& document) {
        document.last_run_version = version(1);
        document.declined_integrations = {IntegrationKind::UrlScheme};
    });
    f.registrar.set_state(IntegrationKind::EngineAgent, IntegrationState::Stale);
    REQUIRE(f.registrar.apply(IntegrationKind::Autostart, NativePath("C:/Old/reboot-engine.exe")));
    f.registrar.set_state(IntegrationKind::Autostart, IntegrationState::Stale);

    const std::optional<ReconcileReport> report = f.reconcile(version(1, 1));
    REQUIRE(report);
    CHECK(report->previous == version(1));
    // Autostart is opt-in, but an existing one of ours with old arguments is still rewritten.
    CHECK(report->written == std::vector{IntegrationKind::Autostart, IntegrationKind::EngineAgent});
    CHECK(of(report->items, IntegrationKind::UrlScheme).state == EntryState::Absent);
    CHECK(of(report->items, IntegrationKind::UrlScheme).declined);
    CHECK(f.registrar.applied_exe(IntegrationKind::Autostart) == NativePath("C:/Reboot/reboot-engine.exe"));
    CHECK_FALSE(f.registrar.applied_exe(IntegrationKind::UrlScheme));
}

TEST_CASE("the version is recorded even when every write fails", "[integration][reconcile]") {
    Fixture f;
    f.registrar.faults().fail_always(RegistrarOperation::Apply, test::fault("platform.access_denied"));
    const std::optional<ReconcileReport> report = f.reconcile(version(2));
    REQUIRE(report);
    CHECK(report->written.empty());
    REQUIRE(of(report->items, IntegrationKind::UrlScheme).detail);
    CHECK(of(report->items, IntegrationKind::UrlScheme).detail->id == "integration.write_failed");
    CHECK(f.state.get().last_run_version == version(2));
    CHECK(f.changes().empty());
}

TEST_CASE("a cancelled reconcile records nothing", "[integration][reconcile]") {
    Fixture f;
    CancelSource source;
    source.cancel(CancelReason::Shutdown);
    CHECK_FALSE(f.reconcile(version(3), source.token()));
    CHECK_FALSE(f.state.get().last_run_version);
    CHECK_FALSE(f.registrar.applied_exe(IntegrationKind::UrlScheme));
}

TEST_CASE("a reconcile queued behind an apply sees what the apply wrote", "[integration][reconcile]") {
    Fixture f;
    const Result<OpHandle> applied = f.service.start_apply({IntegrationKind::UrlScheme}, DisconnectPolicy::Detached);
    REQUIRE(applied);
    const std::optional<ReconcileReport> report = f.reconcile(version(1));
    REQUIRE(report);
    CHECK(f.rig.ops.outcome(applied->id()).has_value());
    CHECK(report->written == std::vector{IntegrationKind::EngineAgent});
}
