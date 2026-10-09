#include <chrono>
#include <string>
#include <thread>
#include <vector>

#include <catch2/catch_message.hpp>
#include <catch2/catch_test_macros.hpp>

#include "reboot/foundation/native_path.hpp"
#include "reboot/ports/file_system.hpp"
#include "reboot/testing/deterministic_runtime.hpp"
#include "reboot/testing/fake_file_watcher.hpp"
#include "reboot/testing/fake_platform_paths.hpp"
#include "reboot/testing/in_memory_file_system.hpp"
#include "reboot/testing/manual_waiter.hpp"
#include "reboot/testing/port_conformance.hpp"

using namespace rb;
using namespace rb::testing;
using namespace std::chrono_literals;

namespace {

void require_passed(const ConformanceReport& report) {
    INFO(report.describe());
    REQUIRE(report.passed());
}

[[nodiscard]] std::vector<u8> bytes(std::string_view text) { return {text.begin(), text.end()}; }

const NativePath kRoot = default_fake_root();

}  // namespace

TEST_CASE("InMemoryFileSystem passes the file system suite", "[testing][conformance][fs]") {
    DeterministicRuntime runtime;
    ManualWaiter waiter(runtime);
    InMemoryFileSystem fs;
    FileSystemConformanceHooks hooks;
    hooks.make_symlink = [&fs](const NativePath& link, const NativePath& target) -> Result<void> {
        fs.make_symlink(link, target);
        return {};
    };
    require_passed(run_file_system_conformance(fs, {waiter, kRoot / "scratch"}, std::move(hooks)));
    CHECK_FALSE(fs.exists(kRoot / "scratch" / "fs-conformance"));
}

TEST_CASE("FakeFileWatcher following InMemoryFileSystem passes the watcher suite", "[testing][conformance][fs]") {
    DeterministicRuntime runtime;
    ManualWaiter waiter(runtime);
    InMemoryFileSystem fs(runtime.clock());
    FakeFileWatcher watcher(runtime.strand());
    watcher.follow(fs);
    require_passed(run_file_watcher_conformance(watcher, fs, {waiter, kRoot / "scratch"}));
    CHECK(watcher.live_watches() == 0);
}

TEST_CASE("atomic_replace keeps a backup and reports changes", "[testing][fs]") {
    InMemoryFileSystem fs;
    std::vector<ports::FileChange> changes;
    fs.set_change_listener([&](const ports::FileChange& change) { changes.push_back(change); });
    const NativePath file = kRoot / "data" / "doc.json";
    fs.make_dir(file.parent_path());
    changes.clear();

    REQUIRE(fs.atomic_replace(file, bytes("one"), true));
    CHECK_FALSE(fs.exists(kRoot / "data" / "doc.json.bak"));
    REQUIRE(fs.atomic_replace(file, bytes("two"), true));
    CHECK(fs.text(kRoot / "data" / "doc.json.bak") == "one");
    CHECK(fs.text(file) == "two");
    REQUIRE(changes.size() == 3);
    CHECK(changes[0].kind == ports::FileChangeKind::Created);
    CHECK(changes[1].path == kRoot / "data" / "doc.json.bak");
    CHECK(changes[2].kind == ports::FileChangeKind::Modified);

    REQUIRE(fs.remove_tree(kRoot / "data"));
    CHECK(changes.back().kind == ports::FileChangeKind::Removed);
    CHECK(changes.back().path == kRoot / "data");
    CHECK(fs.list(kRoot).empty());
}

TEST_CASE("a crash during replace leaves only the temporary file", "[testing][fs]") {
    InMemoryFileSystem fs;
    const NativePath file = kRoot / "state.json";
    fs.write_text(file, "before");
    fs.crash_during_next_replace(file);
    const auto crashed = fs.atomic_replace(file, bytes("after"), false);
    REQUIRE_FALSE(crashed);
    CHECK(crashed.error().id == "testing.simulated_crash");
    CHECK(fs.text(file) == "before");
    const auto entries = fs.list(kRoot);
    REQUIRE(entries.size() == 2);
    CHECK(entries[1].filename().string().starts_with("state.json.tmp"));

    REQUIRE(fs.atomic_replace(file, bytes("after"), false));
    CHECK(fs.text(file) == "after");
}

TEST_CASE("a held file cannot be replaced or removed until it is closed", "[testing][fs]") {
    InMemoryFileSystem fs;
    const NativePath file = kRoot / "game" / "shipping.exe";
    fs.write_text(file, "MZ");
    {
        auto held = fs.open_deny_write(file);
        REQUIRE(held);
        const auto replaced = fs.atomic_replace(file, bytes("patched"), false);
        REQUIRE_FALSE(replaced);
        CHECK(replaced.error().kind == ErrorKind::Conflict);
        CHECK_FALSE(fs.remove_tree(kRoot / "game"));
        CHECK(fs.exists(file));
    }
    CHECK(fs.atomic_replace(file, bytes("patched"), false));
    CHECK(fs.remove_tree(kRoot / "game"));
}

TEST_CASE("locks are exclusive and a waiting lock times out on its limit", "[testing][fs]") {
    InMemoryFileSystem fs;
    const NativePath lock_path = kRoot / "state" / "engine.lock";
    fs.make_dir(lock_path.parent_path());
    auto first = fs.lock_exclusive(lock_path, false);
    REQUIRE(first);
    CHECK(fs.locked(lock_path));
    CHECK(fs.exists(lock_path));

    const auto immediate = fs.lock_exclusive(lock_path, true);
    REQUIRE_FALSE(immediate);
    CHECK(immediate.error().id == "testing.lock_wait_exceeded");

    fs.set_lock_wait_limit(5s);
    std::thread releaser([&first] {
        std::this_thread::sleep_for(20ms);
        first->release();
    });
    const auto waited = fs.lock_exclusive(lock_path, true);
    releaser.join();
    CHECK(waited);
}

TEST_CASE("links are followed for reads and never by remove_tree", "[testing][fs]") {
    InMemoryFileSystem fs;
    fs.write_text(kRoot / "real" / "file.txt", "kept");
    fs.make_symlink(kRoot / "tree" / "link", NativePath("..") / "real");
    const auto through = fs.read_all(kRoot / "tree" / "link" / "file.txt");
    REQUIRE(through);
    CHECK(std::string(through->begin(), through->end()) == "kept");
    CHECK(fs.is_dir(kRoot / "tree" / "link"));
    CHECK_FALSE(fs.restrict_to_owner(kRoot / "tree" / "link"));

    REQUIRE(fs.remove_tree(kRoot / "tree"));
    CHECK(fs.text(kRoot / "real" / "file.txt") == "kept");
}

TEST_CASE("create_dirs_owner_only marks only what it creates", "[testing][fs]") {
    InMemoryFileSystem fs;
    fs.make_dir(kRoot / "data");
    REQUIRE(fs.create_dirs_owner_only(kRoot / "data" / "state" / "host-identity"));
    CHECK_FALSE(fs.owner_only(kRoot / "data"));
    CHECK(fs.owner_only(kRoot / "data" / "state"));
    CHECK(fs.owner_only(kRoot / "data" / "state" / "host-identity"));

    fs.write_text(kRoot / "data" / "file", "x");
    const auto through_file = fs.create_dirs_owner_only(kRoot / "data" / "file" / "below");
    REQUIRE_FALSE(through_file);
    CHECK(through_file.error().id == "testing.not_a_directory");
}

TEST_CASE("revisions follow the clock and change on every write", "[testing][fs]") {
    DeterministicRuntime runtime;
    InMemoryFileSystem fs(runtime.clock());
    const NativePath file = kRoot / "a.json";
    fs.make_dir(kRoot);
    REQUIRE(fs.atomic_replace(file, bytes("1"), false));
    const auto first = fs.revision(file);
    runtime.clock().advance(3s);
    REQUIRE(fs.atomic_replace(file, bytes("1"), false));
    const auto second = fs.revision(file);
    REQUIRE((first && second));
    CHECK(second->mtime - first->mtime == 3s);
    CHECK(second->file_id != first->file_id);
    CHECK(fs.revision(kRoot / "missing").error().kind == ErrorKind::NotFound);
}

TEST_CASE("faults fail the chosen operation only", "[testing][fs]") {
    InMemoryFileSystem fs;
    fs.write_text(kRoot / "x", "x");
    fs.faults().fail_next(FsOperation::ReadAll, make_diag(ErrorDomain::Internal, MessageId{"internal.bug"}));
    CHECK_FALSE(fs.read_all(kRoot / "x"));
    CHECK(fs.read_all(kRoot / "x"));
    CHECK(fs.contents(kRoot / "x"));
}

TEST_CASE("FakeFileWatcher drops changes while overflowing and after the handle goes", "[testing][fs]") {
    DeterministicRuntime runtime;
    FakeFileWatcher watcher(runtime.strand());
    std::vector<ports::FileChange> seen;
    auto handle = watcher.watch(kRoot / "dir", [&](ports::FileChange change) { seen.push_back(std::move(change)); });
    REQUIRE(handle);

    watcher.emit({kRoot / "elsewhere" / "file", ports::FileChangeKind::Created});
    watcher.emit({kRoot / "dir" / "sub" / "file", ports::FileChangeKind::Created});
    runtime.run_until_idle();
    REQUIRE(seen.size() == 1);

    watcher.set_overflowing(true);
    watcher.emit({kRoot / "dir" / "lost", ports::FileChangeKind::Modified});
    watcher.set_overflowing(false);
    runtime.run_until_idle();
    CHECK(seen.size() == 1);

    // Posted before the handle went, never delivered after it.
    watcher.emit({kRoot / "dir" / "late", ports::FileChangeKind::Modified});
    *handle = ports::WatchHandle{};
    runtime.run_until_idle();
    CHECK(seen.size() == 1);
    CHECK(watcher.live_watches() == 0);

    watcher.fail_next_watch(make_diag(ErrorDomain::Internal, MessageId{"internal.bug"}));
    CHECK_FALSE(watcher.watch(kRoot, [](ports::FileChange) {}));
    CHECK(watcher.watch(kRoot, [](ports::FileChange) {}));
}
