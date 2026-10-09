#include <catch2/catch_test_macros.hpp>
#include <vector>

#include "reboot/components/deletion_guard.hpp"
#include "reboot/foundation/clock.hpp"
#include "reboot/foundation/executor.hpp"
#include "reboot/ports/file_system.hpp"
#include "reboot/testing/fake_file_watcher.hpp"

using namespace rb;
using namespace rb::components;
using ports::FileChange;
using ports::FileChangeKind;

namespace {

struct Fixture {
    ManualClock clock;
    ManualExecutor strand{clock};
    testing::FakeFileWatcher watcher{strand};
    std::vector<NativePath> reports;
    DeletionGuard guard{watcher, strand, [this](const NativePath& file) { reports.push_back(file); }};

    void emit(const NativePath& path, FileChangeKind kind) {
        watcher.emit(FileChange{path, kind});
        strand.run_all();
    }
};

const NativePath kDir = NativePath("/data/components/payload/aa");
const NativePath kClient = kDir / "rb_client.dll";
const NativePath kWinhost = kDir / "reboot-winhost.exe";

}  // namespace

TEST_CASE("a tracked file that is removed, renamed or modified is reported", "[components][guard]") {
    Fixture f;
    REQUIRE(f.guard.track(kClient));
    CHECK(f.watcher.live_watches() == 1);
    f.emit(kClient, FileChangeKind::Removed);
    f.emit(kClient, FileChangeKind::Renamed);
    f.emit(kClient, FileChangeKind::Modified);
    CHECK(f.reports == std::vector{kClient, kClient, kClient});
}

TEST_CASE("creations and untracked neighbours are not reported", "[components][guard]") {
    Fixture f;
    REQUIRE(f.guard.track(kClient));
    f.emit(kClient, FileChangeKind::Created);
    f.emit(kDir / "other.dll", FileChangeKind::Removed);
    CHECK(f.reports.empty());
}

TEST_CASE("one watch serves every tracked file of a directory until the last is untracked", "[components][guard]") {
    Fixture f;
    REQUIRE(f.guard.track(kClient));
    REQUIRE(f.guard.track(kWinhost));
    CHECK(f.watcher.live_watches() == 1);
    f.guard.untrack(kClient);
    CHECK(f.watcher.live_watches() == 1);
    f.emit(kClient, FileChangeKind::Removed);
    f.emit(kWinhost, FileChangeKind::Removed);
    CHECK(f.reports == std::vector{kWinhost});
    f.guard.untrack(kWinhost);
    CHECK(f.watcher.live_watches() == 0);
}

TEST_CASE("a change posted before its watch ended is dropped, even once the directory is watched again",
          "[components][guard]") {
    Fixture f;
    REQUIRE(f.guard.track(kClient));
    f.watcher.emit(FileChange{kClient, FileChangeKind::Removed});
    f.guard.untrack(kClient);
    REQUIRE(f.guard.track(kClient));
    f.strand.run_all();
    CHECK(f.reports.empty());
}

TEST_CASE("a failed watch is returned and leaves nothing tracked", "[components][guard]") {
    Fixture f;
    f.watcher.fail_next_watch(make_diag(ErrorDomain::Platform, MessageId{"components_test.watch_failed"}).build());
    const auto tracked = f.guard.track(kClient);
    REQUIRE_FALSE(tracked);
    CHECK(tracked.error().id == "components_test.watch_failed");
    REQUIRE(f.guard.track(kClient));
    CHECK(f.watcher.live_watches() == 1);
}
