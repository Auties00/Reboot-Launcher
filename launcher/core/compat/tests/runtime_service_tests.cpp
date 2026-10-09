#include <catch2/catch_test_macros.hpp>

#include <any>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "compat_test_support.hpp"
#include "reboot/compat/compat_document.hpp"
#include "reboot/compat/prefix_manager.hpp"
#include "reboot/compat/rosetta_install_answer.hpp"
#include "reboot/compat/rosetta_install_request.hpp"
#include "reboot/foundation/events.hpp"
#include "reboot/foundation/operation.hpp"
#include "reboot/foundation/paths.hpp"
#include "reboot/foundation/user_request.hpp"
#include "reboot/storage/document_store.hpp"
#include "reboot/testing/fake_os.hpp"
#include "reboot/testing/fake_platform_paths.hpp"
#include "reboot/testing/fake_runner_platform.hpp"
#include "reboot/testing/in_memory_file_system.hpp"
#include "reboot/testing/scripted_process_launcher.hpp"
#include "runtime_core.hpp"

using namespace rb;
using namespace rb::compat;
using rb::compat::test::TestStrand;
using rb::components::RuntimeKind;

namespace {

constexpr MessageId kFetchFailed{"compat_test.fetch_failed"};

// The manifest's runtimes and the store's pins; a pin's root is <root>/<id>.
class FakeCatalog final : public RuntimeCatalog {
public:
    FakeCatalog(Executor& strand, NativePath root) : strand_(strand), root_(std::move(root)) {}

    [[nodiscard]] std::vector<components::RuntimeEntry> runtimes() const override { return entries; }

    void acquire(SessionId, std::string_view runtime_id, CancelToken token, components::ProgressSink progress,
                 UniqueFunction<void(Result<components::PinnedRuntime>)> done) override {
        acquired.emplace_back(runtime_id);
        std::optional<components::RuntimeEntry> entry;
        for (const components::RuntimeEntry& candidate : entries)
            if (candidate.id == runtime_id) entry = candidate;
        Result<components::PinnedRuntime> result = std::unexpected(Diagnostic{});
        if (fail_fetch || !entry) {
            result = std::unexpected(make_diag(ErrorDomain::Internal, kFetchFailed).build());
        } else {
            components::PinnedRuntime pinned;
            pinned.runtime.ref = {components::ComponentKind::Runtime, entry->id, entry->version};
            pinned.runtime.kind = entry->kind;
            pinned.runtime.root = root_ / entry->id;
            result = std::move(pinned);
        }
        strand_.post([token, progress = std::move(progress), done = std::move(done), result = std::move(result)]() mutable {
            if (progress) progress(Progress{.phase = "download"});
            if (token.cancelled())
                return done(std::unexpected(make_diag(ErrorDomain::Internal, kFetchFailed).kind(ErrorKind::Cancelled).build()));
            done(std::move(result));
        });
    }

    void mark_good(const components::ComponentPin&) override { ++marked_good; }

    std::vector<components::RuntimeEntry> entries;
    std::vector<std::string> acquired;
    bool fail_fetch = false;
    int marked_good = 0;

private:
    Executor& strand_;
    NativePath root_;
};

[[nodiscard]] components::RuntimeEntry entry(std::string id, RuntimeKind kind, std::string version) {
    components::RuntimeEntry out;
    out.id = std::move(id);
    out.kind = kind;
    out.version = std::move(version);
    return out;
}

struct Fixture {
    explicit Fixture(std::vector<RunnerKind> kinds = {RunnerKind::Umu, RunnerKind::Wine}) : runner(std::move(kinds)) {
        static_cast<void>(document.load());
        ports::RuntimeLayout wine;
        wine.entry = "bin/wine";
        runner.set_layout(RunnerKind::Wine, wine);
        runner.set_layout(RunnerKind::MacRuntime, wine);
        ports::RuntimeLayout umu;
        umu.entry = "umu-run";
        umu.env = {{"PROTONPATH", "proton"}};
        runner.set_layout(RunnerKind::Umu, umu);
        catalog.entries = {entry("kron-11", RuntimeKind::KronWine, "11.0"),
                           entry("GE-Proton10-25", RuntimeKind::GeProton, "10-25"),
                           entry("GE-Proton11-7", RuntimeKind::GeProton, "11-7"),
                           entry("umu-1.4.4", RuntimeKind::Umu, "1.4.4"),
                           entry("mac-wine-11", RuntimeKind::MacWine, "11.0"),
                           entry("vc-14", RuntimeKind::VcRedist, "14.40")};
    }

    ~Fixture() { workers.shutdown(); }

    struct Run {
        Operation<SessionId>* op = nullptr;
        std::optional<Result<PreparedRuntime>> result;
    };

    // Starts a prepare under a fresh play op; the test drives the strand.
    std::unique_ptr<Run> start(RunnerKind kind, SessionId session = SessionId{Uuid{{7}}}) {
        auto run = std::make_unique<Run>();
        auto [handle, op] = ops.create<SessionId>(OpKind::Play, DisconnectPolicy::Detached, session);
        run->op = &op;
        auto profile = core.profile(kind);
        REQUIRE(profile);
        core.prepare(session, *profile, op, nullptr, [raw = run.get()](Result<PreparedRuntime> r) { raw->result = std::move(r); });
        return run;
    }

    Result<PreparedRuntime> prepare(RunnerKind kind) {
        auto run = start(kind);
        strand.run_until([&] { return run->result.has_value(); });
        return std::move(*run->result);
    }

    ErasedOutcome setup(RunnerKind kind) {
        auto handle = core.start_setup(kind, DisconnectPolicy::Detached);
        REQUIRE(handle);
        strand.run_until([&] { return ops.outcome(handle->id()).has_value(); });
        return *ops.outcome(handle->id());
    }

    [[nodiscard]] const RuntimeRecord* record(std::string_view id) const {
        for (const RuntimeRecord& r : document.get().runtimes)
            if (r.runtime.value == id) return &r;
        return nullptr;
    }

    NativePath root = NativePath("rt");
    ManualClock clock;
    TestStrand strand{clock};
    TimerService timers{clock, strand};
    WorkerPool workers{1};
    EventBus events{EngineEpoch{1}};
    OpRegistry ops{clock, timers, events};
    UserRequestRegistry requests{events};
    testing::InMemoryFileSystem files;
    storage::DocumentStore<CompatDocument> document{files, workers, strand, clock, NativePath("/state/compat.json")};
    testing::ScriptedProcessLauncher processes{strand, clock, testing::FakeOs::Linux};
    testing::FakeRunnerPlatform runner;
    testing::FakePlatformPaths paths{NativePath("/home")};
    AppLayout layout{DataRoot{NativePath("/home/data"), true}, paths};
    PrefixManager prefixes{PrefixManagerDeps{processes, runner, files, workers, strand, timers, document}, layout};
    FakeCatalog catalog{strand, root};
    RuntimeCore core{RuntimeCoreDeps{catalog, runner, prefixes, requests, ops, workers, strand, clock, document}};
};

}  // namespace

TEST_CASE("the runners are the platform's, best first", "[compat][runtime_service]") {
    Fixture f;
    CHECK(f.core.supported() == std::vector{RunnerKind::Umu, RunnerKind::Wine});
}

TEST_CASE("a profile names the newest runtime the manifest selects", "[compat][runtime_service]") {
    Fixture f;
    const auto umu = f.core.profile(RunnerKind::Umu);
    REQUIRE(umu);
    CHECK(umu->runtime == RuntimeId{"GE-Proton11-7"});
    CHECK(umu->launcher == RuntimeId{"umu-1.4.4"});
    CHECK(umu->multiplier() == RunnerMultiplier::Wine);

    const auto wine = f.core.profile(RunnerKind::Wine);
    REQUIRE(wine);
    CHECK(wine->runtime == RuntimeId{"kron-11"});
    CHECK_FALSE(wine->launcher);
}

TEST_CASE("a runner the platform lacks, or one with no runtime, has no profile", "[compat][runtime_service]") {
    Fixture f;
    for (const RunnerKind kind : {RunnerKind::Native, RunnerKind::MacRuntime}) {
        const auto profile = f.core.profile(kind);
        REQUIRE_FALSE(profile);
        CHECK(profile.error().id == "compat.runner_unsupported");
    }
    f.catalog.entries.erase(f.catalog.entries.begin() + 3);
    const auto umu = f.core.profile(RunnerKind::Umu);
    REQUIRE_FALSE(umu);
    CHECK(umu.error().id == "compat.no_runtime");
}

TEST_CASE("a Wine prepare pins its runtime and holds the prefix", "[compat][runtime_service]") {
    Fixture f;
    auto prepared = f.prepare(RunnerKind::Wine);
    REQUIRE(prepared);
    CHECK(prepared->wine.runtime.ref.id == "kron-11");
    CHECK_FALSE(prepared->launcher);
    CHECK(prepared->layout.entry == f.root / "kron-11" / "bin/wine");
    CHECK(prepared->lease.held());
    CHECK(f.runner.post_extracted() == std::vector<NativePath>{f.root / "kron-11"});
    CHECK_FALSE(f.prefixes.idle(RunnerKind::Wine));
    prepared->lease.release();
    CHECK(f.prefixes.idle(RunnerKind::Wine));
}

TEST_CASE("a failed fetch fails the prepare and frees the prefix", "[compat][runtime_service]") {
    Fixture f;
    f.catalog.fail_fetch = true;
    const auto prepared = f.prepare(RunnerKind::Wine);
    REQUIRE_FALSE(prepared);
    CHECK(prepared.error().id == "compat_test.fetch_failed");
    CHECK(f.prefixes.idle(RunnerKind::Wine));
}

TEST_CASE("umu plays only after its runtime setup, which records the runtime build", "[compat][runtime_service]") {
    Fixture f;
    const auto refused = f.prepare(RunnerKind::Umu);
    REQUIRE_FALSE(refused);
    CHECK(refused.error().id == "compat.runtime_setup_required");
    CHECK(f.prefixes.idle(RunnerKind::Umu));

    f.runner.set_setup_build("steamrt3 0.20250210.116596");
    f.clock.set_system(std::chrono::system_clock::time_point{std::chrono::seconds{1'700'000'000}});
    const ErasedOutcome outcome = f.setup(RunnerKind::Umu);
    const auto* completed = std::get_if<Completed<std::any>>(&outcome);
    REQUIRE(completed != nullptr);
    const auto refs = std::any_cast<std::vector<components::ComponentRef>>(completed->value);
    REQUIRE(refs.size() == 2);
    CHECK(refs[0].id == "GE-Proton11-7");
    CHECK(refs[1].id == "umu-1.4.4");
    CHECK(f.runner.runtime_setups() == 1);
    const RuntimeRecord* record = f.record("umu-1.4.4");
    REQUIRE(record != nullptr);
    REQUIRE(record->slr);
    CHECK(record->slr->build == "steamrt3 0.20250210.116596");
    CHECK(record->slr->installed_at == std::chrono::system_clock::time_point{std::chrono::seconds{1'700'000'000}});

    auto prepared = f.prepare(RunnerKind::Umu);
    REQUIRE(prepared);
    REQUIRE(prepared->launcher);
    CHECK(prepared->launcher->runtime.ref.id == "umu-1.4.4");
    CHECK(prepared->layout.entry == f.root / "umu-1.4.4" / "umu-run");
}

TEST_CASE("a runtime setup never runs beside a session", "[compat][runtime_service]") {
    Fixture f;
    auto lease = f.prefixes.lease(RunnerKind::Wine, SessionId{});
    REQUIRE(lease);
    const auto in_use = f.core.start_setup(RunnerKind::Wine, DisconnectPolicy::Detached);
    REQUIRE_FALSE(in_use);
    CHECK(in_use.error().id == "compat.runtime_in_use");
    lease->release();

    const auto first = f.core.start_setup(RunnerKind::Wine, DisconnectPolicy::Detached);
    REQUIRE(first);
    const auto second = f.core.start_setup(RunnerKind::Wine, DisconnectPolicy::Detached);
    REQUIRE_FALSE(second);
    CHECK(second.error().id == "compat.runtime_setup_running");
    auto run = f.start(RunnerKind::Wine);
    f.strand.run_until([&] { return run->result.has_value() && f.ops.outcome(first->id()).has_value(); });
    REQUIRE_FALSE(*run->result);
    CHECK(run->result->error().id == "compat.runtime_setup_running");
    // Only Umu has a runtime to set up.
    CHECK(std::holds_alternative<Completed<std::any>>(*f.ops.outcome(first->id())));
    CHECK(f.runner.runtime_setups() == 0);
}

TEST_CASE("a failed runtime setup fails its op", "[compat][runtime_service]") {
    Fixture f;
    f.runner.faults().fail_next(testing::RunnerOperation::RuntimeSetup, internal_bug("test"));
    const ErasedOutcome outcome = f.setup(RunnerKind::Umu);
    const auto* failed = std::get_if<Failed>(&outcome);
    REQUIRE(failed != nullptr);
    CHECK(failed->error.id == "compat.runtime_setup_failed");
    REQUIRE(failed->error.causes.size() == 1);
    CHECK(failed->error.causes[0].id == "internal.bug");
    CHECK(f.record("umu-1.4.4") == nullptr);
    CHECK(f.core.start_setup(RunnerKind::Umu, DisconnectPolicy::Detached));
}

TEST_CASE("a missing Rosetta waits for the user", "[compat][runtime_service]") {
    Fixture f({RunnerKind::MacRuntime});
    f.runner.set_pending_prerequisite(UserRequestKind::RosettaInstall);
    auto run = f.start(RunnerKind::MacRuntime);
    f.strand.run_until([&] { return !f.requests.pending().empty(); });
    const UserRequest request = f.requests.pending().front();
    CHECK(request.kind == UserRequestKind::RosettaInstall);
    CHECK(request.op == run->op->id());
    CHECK(std::any_cast<RosettaInstallRequest>(&request.payload) != nullptr);

    SECTION("Installed is checked again") {
        const auto early = f.requests.respond(request.id, RosettaInstallAnswer::Installed);
        REQUIRE_FALSE(early);
        CHECK(early.error().id == "compat.rosetta_missing");
        CHECK(f.requests.pending().size() == 1);

        const auto wrong = f.requests.respond(request.id, 42);
        REQUIRE_FALSE(wrong);
        CHECK(wrong.error().id == "compat.answer_invalid");

        f.runner.set_pending_prerequisite(std::nullopt);
        REQUIRE(f.requests.respond(request.id, RosettaInstallAnswer::Installed));
        f.strand.run_until([&] { return run->result.has_value(); });
        REQUIRE(*run->result);
        CHECK((*run->result)->profile.rosetta_cold);
    }

    SECTION("Declined fails the prepare") {
        REQUIRE(f.requests.respond(request.id, RosettaInstallAnswer::Declined));
        f.strand.run_until([&] { return run->result.has_value(); });
        REQUIRE_FALSE(*run->result);
        CHECK(run->result->error().id == "compat.rosetta_declined");
        CHECK(f.prefixes.idle(RunnerKind::MacRuntime));
    }

    SECTION("cancelling the op withdraws the request") {
        REQUIRE(f.ops.cancel(run->op->id(), CancelReason::User));
        f.strand.run_until([&] { return run->result.has_value(); });
        REQUIRE_FALSE(*run->result);
        CHECK(run->result->error().kind == ErrorKind::Cancelled);
        CHECK(f.requests.pending().empty());
        CHECK(f.prefixes.idle(RunnerKind::MacRuntime));
    }
}

TEST_CASE("a completed session ends the Rosetta first run and lets the store collect", "[compat][runtime_service]") {
    Fixture f({RunnerKind::MacRuntime});
    CHECK(f.core.profile(RunnerKind::MacRuntime)->multiplier() == RunnerMultiplier::RosettaFirstRun);
    auto prepared = f.prepare(RunnerKind::MacRuntime);
    REQUIRE(prepared);
    f.core.mark_good(*prepared);
    CHECK(f.catalog.marked_good == 1);
    REQUIRE(f.record("mac-wine-11") != nullptr);
    CHECK(f.record("mac-wine-11")->completed_session);
    CHECK(f.core.profile(RunnerKind::MacRuntime)->multiplier() == RunnerMultiplier::Wine);
}

TEST_CASE("the VC++ source pins the manifest's redistributable to the session", "[compat][runtime_service]") {
    Fixture f;
    std::optional<Result<components::PinnedRuntime>> pinned;
    VcRedistSource source = f.core.vc_redist_source(SessionId{Uuid{{3}}}, {});
    source([&](Result<components::PinnedRuntime> r) { pinned = std::move(r); });
    f.strand.run_until([&] { return pinned.has_value(); });
    REQUIRE(*pinned);
    CHECK((*pinned)->runtime.ref.id == "vc-14");

    f.catalog.entries.pop_back();
    std::optional<Result<components::PinnedRuntime>> missing;
    VcRedistSource none = f.core.vc_redist_source(SessionId{}, {});
    none([&](Result<components::PinnedRuntime> r) { missing = std::move(r); });
    REQUIRE(missing);
    REQUIRE_FALSE(*missing);
    CHECK(missing->error().id == "compat.no_vc_redist");
}

TEST_CASE("a fetch that reports after the service is gone reaches nothing", "[compat][runtime_service]") {
    Fixture f;
    auto core = std::make_unique<RuntimeCore>(
        RuntimeCoreDeps{f.catalog, f.runner, f.prefixes, f.requests, f.ops, f.workers, f.strand, f.clock, f.document});
    auto [handle, op] = f.ops.create<SessionId>(OpKind::Play, DisconnectPolicy::Detached, SessionId{Uuid{{9}}});
    const auto profile = core->profile(RunnerKind::Wine);
    REQUIRE(profile);
    int progressed = 0;
    bool finished = false;
    core->prepare(SessionId{Uuid{{9}}}, *profile, op, [&](const Progress&) { ++progressed; },
                  [&](Result<PreparedRuntime>) { finished = true; });
    f.strand.run_until([&] { return !f.catalog.acquired.empty(); });
    core.reset();
    f.strand.drain();
    CHECK(progressed == 0);
    CHECK_FALSE(finished);
    CHECK(f.prefixes.idle(RunnerKind::Wine));
    static_cast<void>(handle);
}
