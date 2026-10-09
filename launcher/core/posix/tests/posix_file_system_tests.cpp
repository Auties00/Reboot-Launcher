#include <catch2/catch_test_macros.hpp>
#include <cerrno>
#include <fcntl.h>
#include <filesystem>
#include <optional>
#include <string>
#include <sys/stat.h>
#include <vector>

#include "messages.hpp"
#include "owner_only_directory.hpp"
#include "posix_test_support.hpp"
#include "reboot/foundation/diag.hpp"
#include "reboot/posix/posix_error.hpp"
#include "reboot/posix/posix_file_system.hpp"
#include "reboot/posix/unique_fd.hpp"
#include "reboot/testing/port_conformance.hpp"
#include "reboot/testing/wall_clock_waiter.hpp"
#include "unistd.hpp"

using namespace rb;
using namespace rb::posix;
using namespace rb::posix::test;

namespace {

[[nodiscard]] Result<void> fsync_fd(int fd) {
    if (::fsync(fd) != 0) return std::unexpected(call_failed("fsync", errno));
    return {};
}

[[nodiscard]] PosixFileSystem make_file_system() { return PosixFileSystem{fsync_fd, fsync_fd}; }

// The file's bytes as text, or "<error>" when it cannot be read.
[[nodiscard]] std::string contents(PosixFileSystem& fs, const NativePath& path) {
    const auto bytes = fs.read_all(path);
    if (!bytes) return "<error>";
    return {bytes->begin(), bytes->end()};
}

void write_raw(const NativePath& path, std::string_view text, int flags = O_WRONLY | O_CREAT | O_TRUNC) {
    const UniqueFd fd{::open(path.c_str(), flags | O_CLOEXEC, 0644)};
    REQUIRE(fd.valid());
    REQUIRE(::write(fd.get(), text.data(), text.size()) == static_cast<ssize_t>(text.size()));
}

[[nodiscard]] std::size_t entries_in(const NativePath& directory) {
    std::size_t count = 0;
    for ([[maybe_unused]] const auto& entry : std::filesystem::directory_iterator(directory)) ++count;
    return count;
}

template <class T>
[[nodiscard]] std::optional<ErrorKind> failure_kind(const Result<T>& result) {
    if (result) return std::nullopt;
    return result.error().kind;
}

[[nodiscard]] Result<std::string> read_held(ports::HeldFile& held) {
    std::string out;
    std::vector<u8> chunk(3);
    for (;;) {
        auto got = held.read(chunk);
        if (!got) return std::unexpected(std::move(got.error()));
        if (*got == 0) return out;
        out.append(reinterpret_cast<const char*>(chunk.data()), *got);
    }
}

}  // namespace

TEST_CASE("PosixFileSystem passes the file system conformance suite") {
    const auto scratch = make_scratch("posix-fs");
    PosixFileSystem fs = make_file_system();
    testing::WallClockWaiter waiter;
    testing::FileSystemConformanceHooks hooks{
        .make_symlink = [](const NativePath& link, const NativePath& target) -> Result<void> {
            if (::symlink(target.c_str(), link.c_str()) != 0) return std::unexpected(call_failed("symlink", errno, link));
            return {};
        }};
    const testing::ConformanceReport report =
        testing::run_file_system_conformance(fs, {.waiter = waiter, .scratch = scratch.path()}, std::move(hooks));
    INFO(report.describe());
    CHECK(report.passed());
}

TEST_CASE("atomic_replace refuses to run without its sync hooks") {
    const auto scratch = make_scratch("posix-fs");
    PosixFileSystem fs{PosixFileSystem::SyncFd{}, fsync_fd};
    const NativePath target = scratch.path() / "doc.json";

    const auto result = fs.atomic_replace(target, as_bytes("{}"), false);
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().id == "internal.bug");
    CHECK_FALSE(path_exists(target));
    CHECK(entries_in(scratch.path()) == 0);
}

TEST_CASE("atomic_replace syncs the temp file before the rename and the directory after it") {
    const auto scratch = make_scratch("posix-fs");
    const NativePath target = scratch.path() / "doc.json";
    write_raw(target, "old");
    std::vector<std::string> steps;
    PosixFileSystem fs{[&](int fd) -> Result<void> {
                           struct stat info {};
                           REQUIRE(::fstat(fd, &info) == 0);
                           CHECK((info.st_mode & 07777U) == 0600U);
                           CHECK(info.st_size == 3);
                           const auto current = make_file_system().read_all(target);
                           steps.push_back("file:" + std::string(current->begin(), current->end()));
                           return {};
                       },
                       [&](int fd) -> Result<void> {
                           struct stat info {};
                           REQUIRE(::fstat(fd, &info) == 0);
                           CHECK(S_ISDIR(info.st_mode));
                           const auto current = make_file_system().read_all(target);
                           steps.push_back("dir:" + std::string(current->begin(), current->end()));
                           return {};
                       }};

    REQUIRE(fs.atomic_replace(target, as_bytes("new"), false).has_value());
    CHECK(steps == std::vector<std::string>{"file:old", "dir:new"});
    CHECK(mode_of(target) == 0600U);
    CHECK(entries_in(scratch.path()) == 1);
}

TEST_CASE("a failed sync leaves the target untouched and removes the temp file") {
    const auto scratch = make_scratch("posix-fs");
    const NativePath target = scratch.path() / "doc.json";
    write_raw(target, "old");
    PosixFileSystem fs{[](int) -> Result<void> { return std::unexpected(call_failed("fsync", EIO)); }, fsync_fd};

    const auto result = fs.atomic_replace(target, as_bytes("new"), true);
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().is(kCallFailed));
    CHECK(result.error().os_error == errno_error(EIO));
    PosixFileSystem reader = make_file_system();
    CHECK(contents(reader, target) == "old");
    CHECK(entries_in(scratch.path()) == 1);
}

TEST_CASE("keep_backup hard-links the previous file and replaces an older backup") {
    const auto scratch = make_scratch("posix-fs");
    PosixFileSystem fs = make_file_system();
    const NativePath target = scratch.path() / "doc.json";
    const NativePath backup = scratch.path() / "doc.json.bak";
    write_raw(backup, "ancient");
    REQUIRE(fs.atomic_replace(target, as_bytes("first"), true).has_value());
    // No previous file: the old backup stays as it was.
    CHECK(contents(fs, backup) == "ancient");

    const auto before = fs.revision(target);
    REQUIRE(before.has_value());
    REQUIRE(fs.atomic_replace(target, as_bytes("second"), true).has_value());
    const auto kept = fs.revision(backup);
    REQUIRE(kept.has_value());
    CHECK(kept->file_id == before->file_id);
    CHECK(contents(fs, backup) == "first");
    CHECK(contents(fs, target) == "second");
}

TEST_CASE("read_all reads past its growth step and reports a missing file as NotFound") {
    const auto scratch = make_scratch("posix-fs");
    PosixFileSystem fs = make_file_system();
    std::string big(200 * 1024 + 17, '\0');
    for (std::size_t i = 0; i < big.size(); ++i) big[i] = static_cast<char>(i * 13 + 1);
    const NativePath path = scratch.path() / "big.bin";
    write_raw(path, big);

    const auto read = fs.read_all(path);
    REQUIRE(read.has_value());
    CHECK(read->size() == big.size());
    CHECK(std::string(read->begin(), read->end()) == big);

    const auto empty_path = scratch.path() / "empty.bin";
    write_raw(empty_path, "");
    const auto empty = fs.read_all(empty_path);
    REQUIRE(empty.has_value());
    CHECK(empty->empty());

    const auto missing = fs.read_all(scratch.path() / "absent");
    REQUIRE_FALSE(missing.has_value());
    CHECK(missing.error().is(kCallFailedOnPath));
    CHECK(missing.error().kind == ErrorKind::NotFound);
}

TEST_CASE("lock_exclusive creates a 0600 lock file and reports a held lock as posix.lock_busy") {
    const auto scratch = make_scratch("posix-fs");
    PosixFileSystem fs = make_file_system();
    const NativePath path = scratch.path() / "engine.lock";

    auto held = fs.lock_exclusive(path, false);
    REQUIRE(held.has_value());
    CHECK(held->held());
    CHECK(mode_of(path) == 0600U);

    const auto busy = fs.lock_exclusive(path, false);
    REQUIRE_FALSE(busy.has_value());
    CHECK(busy.error().is(kLockBusy));
    CHECK(busy.error().kind == ErrorKind::Conflict);

    held->release();
    auto waited = fs.lock_exclusive(path, true);
    REQUIRE(waited.has_value());
    CHECK(waited->held());
}

TEST_CASE("lock_exclusive does not follow a link") {
    const auto scratch = make_scratch("posix-fs");
    PosixFileSystem fs = make_file_system();
    const NativePath target = scratch.path() / "real.lock";
    const NativePath link = scratch.path() / "link.lock";
    REQUIRE(::symlink(target.c_str(), link.c_str()) == 0);

    const auto result = fs.lock_exclusive(link, false);
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().os_error == errno_error(ELOOP));
    CHECK_FALSE(path_exists(target));
}

TEST_CASE("a held file fails once the file at its path is replaced, grown or removed") {
    const auto scratch = make_scratch("posix-fs");
    PosixFileSystem fs = make_file_system();
    const NativePath path = scratch.path() / "payload.dll";
    write_raw(path, "payload");

    SECTION("an untouched file reads in full") {
        auto held = fs.open_deny_write(path);
        REQUIRE(held.has_value());
        const auto read = read_held(*held);
        REQUIRE(read.has_value());
        CHECK(*read == "payload");
    }
    SECTION("replaced") {
        auto held = fs.open_deny_write(path);
        REQUIRE(held.has_value());
        REQUIRE(fs.atomic_replace(path, as_bytes("payload"), false).has_value());
        const auto read = read_held(*held);
        REQUIRE_FALSE(read.has_value());
        CHECK(read.error().is(kHeldFileChanged));
    }
    SECTION("grown in place") {
        auto held = fs.open_deny_write(path);
        REQUIRE(held.has_value());
        write_raw(path, "!", O_WRONLY | O_APPEND);
        const auto read = read_held(*held);
        REQUIRE_FALSE(read.has_value());
        CHECK(read.error().is(kHeldFileChanged));
    }
    SECTION("removed") {
        auto held = fs.open_deny_write(path);
        REQUIRE(held.has_value());
        REQUIRE(::unlink(path.c_str()) == 0);
        const auto read = read_held(*held);
        REQUIRE_FALSE(read.has_value());
        CHECK(read.error().is(kHeldFileChanged));
    }
    SECTION("missing") {
        const auto held = fs.open_deny_write(scratch.path() / "absent.dll");
        REQUIRE_FALSE(held.has_value());
        CHECK(held.error().kind == ErrorKind::NotFound);
    }
}

TEST_CASE("revision reports the size, the inode and a new inode after a replace") {
    const auto scratch = make_scratch("posix-fs");
    PosixFileSystem fs = make_file_system();
    const NativePath path = scratch.path() / "doc.json";
    REQUIRE(fs.atomic_replace(path, as_bytes("12345"), false).has_value());

    const auto first = fs.revision(path);
    REQUIRE(first.has_value());
    struct stat info {};
    REQUIRE(::stat(path.c_str(), &info) == 0);
    CHECK(first->size == 5);
    CHECK(first->file_id == static_cast<u64>(info.st_ino));

    REQUIRE(fs.atomic_replace(path, as_bytes("12345"), false).has_value());
    const auto second = fs.revision(path);
    REQUIRE(second.has_value());
    CHECK(second->file_id != first->file_id);

    CHECK(failure_kind(fs.revision(scratch.path() / "absent")) == ErrorKind::NotFound);
}

TEST_CASE("create_dirs_owner_only makes new components 0700 whatever the umask") {
    const auto scratch = make_scratch("posix-fs");
    PosixFileSystem fs = make_file_system();
    const NativePath existing = scratch.path() / "existing";
    REQUIRE(::mkdir(existing.c_str(), 0755) == 0);
    REQUIRE(::chmod(existing.c_str(), 0755) == 0);

    {
        const UmaskGuard umask{0277};
        REQUIRE(fs.create_dirs_owner_only(existing / "a" / "b").has_value());
    }
    CHECK(mode_of(existing) == 0755U);
    CHECK(mode_of(existing / "a") == 0700U);
    CHECK(mode_of(existing / "a" / "b") == 0700U);
    // Idempotent.
    CHECK(fs.create_dirs_owner_only(existing / "a" / "b").has_value());

    write_raw(scratch.path() / "file", "x");
    const auto through_file = fs.create_dirs_owner_only(scratch.path() / "file" / "sub");
    REQUIRE_FALSE(through_file.has_value());
    CHECK(through_file.error().os_error == errno_error(ENOTDIR));
    const auto at_file = fs.create_dirs_owner_only(scratch.path() / "file");
    REQUIRE_FALSE(at_file.has_value());
    CHECK(at_file.error().os_error == errno_error(ENOTDIR));
}

TEST_CASE("make_owner_only_directory reports whether it created the directory") {
    const auto scratch = make_scratch("posix-fs");
    const NativePath directory = scratch.path() / "private";
    {
        const UmaskGuard umask{0277};
        const auto made = make_owner_only_directory(directory);
        REQUIRE(made.has_value());
        CHECK(*made);
    }
    CHECK(mode_of(directory) == 0700U);
    const auto again = make_owner_only_directory(directory);
    REQUIRE(again.has_value());
    CHECK_FALSE(*again);

    const auto orphan = make_owner_only_directory(scratch.path() / "missing" / "child");
    REQUIRE_FALSE(orphan.has_value());
    CHECK(orphan.error().kind == ErrorKind::NotFound);
}

TEST_CASE("restrict_to_owner sets 0600 or 0700 and refuses a link") {
    const auto scratch = make_scratch("posix-fs");
    PosixFileSystem fs = make_file_system();
    const NativePath file = scratch.path() / "secret.bin";
    const NativePath directory = scratch.path() / "dir";
    const NativePath link = scratch.path() / "link";
    write_raw(file, "x");
    REQUIRE(::chmod(file.c_str(), 0644) == 0);
    REQUIRE(::mkdir(directory.c_str(), 0755) == 0);
    REQUIRE(::chmod(directory.c_str(), 0755) == 0);
    REQUIRE(::symlink(file.c_str(), link.c_str()) == 0);

    REQUIRE(fs.restrict_to_owner(file).has_value());
    REQUIRE(fs.restrict_to_owner(directory).has_value());
    CHECK(mode_of(file) == 0600U);
    CHECK(mode_of(directory) == 0700U);

    REQUIRE(::chmod(file.c_str(), 0644) == 0);
    const auto through_link = fs.restrict_to_owner(link);
    REQUIRE_FALSE(through_link.has_value());
    CHECK(through_link.error().os_error == errno_error(ELOOP));
    CHECK(mode_of(file) == 0644U);

    CHECK(failure_kind(fs.restrict_to_owner(scratch.path() / "absent")) == ErrorKind::NotFound);
}

TEST_CASE("remove_tree removes nested trees, files and links but never what a link points to") {
    const auto scratch = make_scratch("posix-fs");
    PosixFileSystem fs = make_file_system();
    const NativePath outside = scratch.path() / "outside";
    const NativePath tree = scratch.path() / "tree";
    REQUIRE(fs.create_dirs_owner_only(outside).has_value());
    write_raw(outside / "keep", "keep");
    REQUIRE(fs.create_dirs_owner_only(tree / "a" / "b" / "c").has_value());
    for (int i = 0; i < 50; ++i) write_raw(tree / "a" / ("f" + std::to_string(i)), "x");
    write_raw(tree / "a" / "b" / "c" / "deep", "x");
    REQUIRE(::symlink(outside.c_str(), (tree / "a" / "to-outside").c_str()) == 0);
    REQUIRE(::symlink((outside / "keep").c_str(), (tree / "to-keep").c_str()) == 0);

    REQUIRE(fs.remove_tree(tree).has_value());
    CHECK_FALSE(path_exists(tree));
    CHECK(contents(fs, outside / "keep") == "keep");

    const NativePath top_link = scratch.path() / "top-link";
    REQUIRE(::symlink(outside.c_str(), top_link.c_str()) == 0);
    REQUIRE(fs.remove_tree(top_link).has_value());
    CHECK_FALSE(path_exists(top_link));
    CHECK(path_exists(outside / "keep"));

    REQUIRE(fs.remove_tree(outside / "keep").has_value());
    CHECK_FALSE(path_exists(outside / "keep"));
    CHECK(fs.remove_tree(scratch.path() / "never-made").has_value());
}

TEST_CASE("a trailing separator does not make remove_tree or restrict_to_owner follow a link") {
    const auto scratch = make_scratch("posix-fs");
    PosixFileSystem fs = make_file_system();
    const NativePath outside = scratch.path() / "outside";
    REQUIRE(::mkdir(outside.c_str(), 0755) == 0);
    REQUIRE(::chmod(outside.c_str(), 0755) == 0);
    write_raw(outside / "keep", "keep");
    const NativePath link = scratch.path() / "link";
    REQUIRE(::symlink(outside.c_str(), link.c_str()) == 0);

    for (const std::string suffix : {"/", "/."}) {
        INFO(suffix);
        const auto through_link = fs.restrict_to_owner(NativePath{link.native() + suffix});
        REQUIRE_FALSE(through_link.has_value());
        CHECK(through_link.error().os_error == errno_error(ELOOP));
        CHECK(mode_of(outside) == 0755U);
    }

    REQUIRE(fs.remove_tree(NativePath{link.native() + "/"}).has_value());
    CHECK_FALSE(path_exists(link));
    CHECK(contents(fs, outside / "keep") == "keep");

    REQUIRE(fs.remove_tree(NativePath{outside.native() + "/"}).has_value());
    CHECK_FALSE(path_exists(outside));
}
