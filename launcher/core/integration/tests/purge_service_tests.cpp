#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <future>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "integration_test_support.hpp"
#include "reboot/foundation/executor.hpp"
#include "reboot/foundation/paths.hpp"
#include "reboot/integration/purge_service.hpp"
#include "reboot/testing/fake_platform_paths.hpp"
#include "reboot/testing/in_memory_file_system.hpp"

using namespace reboot;
using namespace reboot::integration;
using testing::FsOperation;

namespace {

// Delegates to the in-memory tree; can hold one remove_tree call until the test opens a gate.
class GatedFileSystem final : public ports::IFileSystem {
public:
    explicit GatedFileSystem(testing::InMemoryFileSystem& inner) : inner_(inner) {}

    Result<void> atomic_replace(const NativePath& target, std::span<const u8> bytes, bool keep_backup) override {
        return inner_.atomic_replace(target, bytes, keep_backup);
    }
    Result<std::vector<u8>> read_all(const NativePath& path) override { return inner_.read_all(path); }
    Result<ports::FileLock> lock_exclusive(const NativePath& path, bool wait) override {
        return inner_.lock_exclusive(path, wait);
    }
    Result<void> restrict_to_owner(const NativePath& path) override { return inner_.restrict_to_owner(path); }
    Result<ports::HeldFile> open_deny_write(const NativePath& path) override { return inner_.open_deny_write(path); }
    Result<ports::FileRevision> revision(const NativePath& path) override { return inner_.revision(path); }
    Result<ports::SharedRead> read_shared(const NativePath& path, u64 offset, std::size_t max_bytes) override {
        return inner_.read_shared(path, offset, max_bytes);
    }
    Result<void> create_dirs_owner_only(const NativePath& path) override {
        return inner_.create_dirs_owner_only(path);
    }
    Result<void> remove_tree(const NativePath& path) override {
        if (held_ && path == *held_) {
            entered_.set_value();
            gate_.wait();
        }
        return inner_.remove_tree(path);
    }

    // Holds the removal of `path`; the returned future is ready once it is held.
    [[nodiscard]] std::future<void> hold(NativePath path, std::shared_future<void> gate) {
        held_ = std::move(path);
        gate_ = std::move(gate);
        return entered_.get_future();
    }

private:
    testing::InMemoryFileSystem& inner_;
    std::optional<NativePath> held_;
    std::shared_future<void> gate_;
    std::promise<void> entered_;
};

struct Fixture {
    explicit Fixture(std::optional<PurgeTargets> custom = std::nullopt)
        : layout(DataRoot{paths.default_data_root(), false}, paths),
          install{.install_dir = paths.exe_dir(),
                  .backend_exe = {},
                  .game_server_exe = {},
                  .backend_content_dir = {},
                  .bundled_catalog = {},
                  .bundled_manifest = {}},
          service(gated, layout, install, custom ? *custom : purge_targets(layout), hooks(), workers, rig.strand,
                  rig.ops) {
        fs.write_text(layout.logs_dir() / "engine.log", "log");
        fs.write_text(layout.backend_dir() / "profiles" / "a.json", "{}");
        fs.write_text(layout.components_dir() / "wine" / "bin", "x");
        fs.write_text(layout.settings_file(), "{}");
    }

    [[nodiscard]] PurgeHooks hooks() {
        return PurgeHooks{
            .find_blockers = [this](PurgeScope) { return blockers; },
            .prepare =
                [this](PurgeScope scope, UniqueFunction<void()> ready) {
                    calls.push_back("prepare");
                    prepared_scope = scope;
                    if (hold_prepare) held_ready = std::move(ready);
                    else ready();
                },
            .on_purged = [this](PurgeScope) { calls.push_back("purged"); },
        };
    }

    [[nodiscard]] ErasedOutcome run(PurgeScope scope) {
        const Result<OpHandle> started = service.start_purge(scope, DisconnectPolicy::Detached);
        REQUIRE(started);
        return rig.wait(started->id());
    }

    testing::FakePlatformPaths paths;
    testing::InMemoryFileSystem fs;
    GatedFileSystem gated{fs};
    test::OpRig rig;
    WorkerPool workers{1};
    PurgeBlockers blockers;
    std::vector<std::string> calls;
    std::optional<PurgeScope> prepared_scope;
    bool hold_prepare = false;
    UniqueFunction<void()> held_ready;
    AppLayout layout;
    InstallLayout install;
    PurgeService service;
};

}  // namespace

TEST_CASE("a purge closes owners' files, deletes the scope and lets owners reopen", "[integration][purge]") {
    Fixture f;
    const PurgeReport report = test::completed_value<PurgeReport>(f.run(PurgeScope::Logs));
    CHECK(report.scope == PurgeScope::Logs);
    CHECK(report.removed == std::vector{f.layout.logs_dir()});
    CHECK(report.absent.empty());
    CHECK_FALSE(f.fs.exists(f.layout.logs_dir()));
    CHECK(f.fs.exists(f.layout.backend_dir() / "profiles" / "a.json"));
    CHECK(f.calls == std::vector<std::string>{"prepare", "purged"});
    CHECK(f.prepared_scope == PurgeScope::Logs);
}

TEST_CASE("All reports missing targets as absent and keeps settings", "[integration][purge]") {
    Fixture f;
    const PurgeReport report = test::completed_value<PurgeReport>(f.run(PurgeScope::All));
    CHECK(report.removed.size() == 3);
    CHECK(report.absent.size() == 3);
    CHECK_FALSE(f.fs.exists(f.layout.components_dir()));
    CHECK_FALSE(f.fs.exists(f.layout.backend_dir()));
    CHECK(f.fs.exists(f.layout.settings_file()));
}

TEST_CASE("any blocker refuses the purge without an op", "[integration][purge]") {
    Fixture f;
    f.blockers.sessions.push_back(SessionId{});
    f.blockers.backend_running = true;
    const Result<OpHandle> started = f.service.start_purge(PurgeScope::BackendData, DisconnectPolicy::Detached);
    REQUIRE_FALSE(started);
    CHECK(started.error().id == "integration.purge_blocked");
    CHECK(started.error().kind == ErrorKind::Conflict);
    CHECK(f.rig.ops.live().empty());
    CHECK(f.calls.empty());
    CHECK(f.fs.exists(f.layout.backend_dir()));
}

TEST_CASE("a root or a folder holding the data root or install is never deleted", "[integration][purge]") {
    const testing::FakePlatformPaths paths;
    const NativePath root = testing::default_fake_root().root_path();
    for (const NativePath& target : {root, paths.default_data_root(), testing::default_fake_root(),
                                     NativePath("relative/logs"), paths.exe_dir()}) {
        Fixture f(PurgeTargets{.backend_data = {}, .logs = {target}, .cache = {}, .components = {},
                               .game_server_sessions = {}});
        const Result<OpHandle> started = f.service.start_purge(PurgeScope::Logs, DisconnectPolicy::Detached);
        REQUIRE_FALSE(started);
        CHECK(started.error().id == "integration.purge_unsafe_target");
        CHECK(f.calls.empty());
        CHECK(f.fs.exists(f.layout.logs_dir() / "engine.log"));
    }
}

TEST_CASE("every target is tried and each failure is a cause", "[integration][purge]") {
    Fixture f;
    f.fs.faults().fail_next(FsOperation::RemoveTree, test::fault("posix.call_failed"));
    const Diagnostic failed = test::failure_of(f.run(PurgeScope::Components));
    CHECK(failed.id == "integration.purge_failed");
    REQUIRE(failed.causes.size() == 1);
    CHECK(failed.causes[0].id == "integration.purge_failed");
    REQUIRE(failed.causes[0].causes.size() == 1);
    CHECK(failed.causes[0].causes[0].id == "posix.call_failed");
    // The components failed; the prefixes were absent and the owners still reopen.
    CHECK(f.fs.exists(f.layout.components_dir()));
    CHECK(f.calls == std::vector<std::string>{"prepare", "purged"});
}

TEST_CASE("a blocker that starts while owners close their files fails the op", "[integration][purge]") {
    Fixture f;
    f.hold_prepare = true;
    const Result<OpHandle> started = f.service.start_purge(PurgeScope::Logs, DisconnectPolicy::Detached);
    REQUIRE(started);
    f.blockers.ops.push_back(OpId{7});
    f.held_ready();
    const Diagnostic failed = test::failure_of(f.rig.wait(started->id()));
    CHECK(failed.id == "integration.purge_blocked");
    CHECK(f.fs.exists(f.layout.logs_dir() / "engine.log"));
    CHECK(f.calls == std::vector<std::string>{"prepare", "purged"});
}

TEST_CASE("a purge in progress blocks another, and a cancel before deletion deletes nothing", "[integration][purge]") {
    Fixture f;
    f.hold_prepare = true;
    const Result<OpHandle> started = f.service.start_purge(PurgeScope::Logs, DisconnectPolicy::Detached);
    REQUIRE(started);
    const Result<OpHandle> second = f.service.start_purge(PurgeScope::Cache, DisconnectPolicy::Detached);
    REQUIRE_FALSE(second);
    CHECK(second.error().id == "integration.purge_blocked");

    REQUIRE(f.rig.ops.cancel(started->id(), CancelReason::User));
    f.held_ready();
    CHECK(std::holds_alternative<Cancelled>(f.rig.wait(started->id())));
    CHECK(f.fs.exists(f.layout.logs_dir() / "engine.log"));
    CHECK(f.calls == std::vector<std::string>{"prepare", "purged"});

    f.hold_prepare = false;
    CHECK(test::completed_value<PurgeReport>(f.run(PurgeScope::Cache)).scope == PurgeScope::Cache);
}

TEST_CASE("a purge counts each target it got through as progress", "[integration][purge]") {
    Fixture f;
    test::Gate gate;
    std::future<void> entered = f.gated.hold(f.layout.components_dir(), gate.future());
    const Result<OpHandle> started = f.service.start_purge(PurgeScope::All, DisconnectPolicy::Detached);
    REQUIRE(started);
    REQUIRE(entered.wait_for(std::chrono::seconds{20}) == std::future_status::ready);
    f.rig.strand.run_ready();

    // Backend data, logs and the cache are behind it; the components are being removed.
    const std::vector<LiveOp> live = f.rig.ops.live();
    REQUIRE(live.size() == 1);
    REQUIRE(live[0].progress);
    CHECK(live[0].progress->phase == "deleting");
    CHECK(live[0].progress->done == 3);
    CHECK(live[0].progress->total == directories_for(purge_targets(f.layout), PurgeScope::All).size());

    gate.open();
    const PurgeReport report = test::completed_value<PurgeReport>(f.rig.wait(started->id()));
    CHECK(report.removed.size() == 3);
}
