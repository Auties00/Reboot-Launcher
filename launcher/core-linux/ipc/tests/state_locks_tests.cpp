#include <catch2/catch_test_macros.hpp>

#include <sys/stat.h>

#include "linux_ipc_test_support.hpp"
#include "state_locks.hpp"

using namespace rb;
using namespace rb::os_linux::ipc;
using namespace rb::os_linux::ipc::test;

namespace {

[[nodiscard]] bool locked(const NativePath& path) {
    const Result<bool> held = is_ofd_locked(path);
    REQUIRE(held);
    return *held;
}

}  // namespace

TEST_CASE("missing directories are created 0700 and existing ones are left alone", "[state_locks]") {
    const auto scratch = make_private_scratch("linux-locks");
    REQUIRE(::mkdir((scratch.path() / "kept").c_str(), 0700) == 0);
    REQUIRE(::chmod((scratch.path() / "kept").c_str(), 0755) == 0);
    const NativePath state = scratch.path() / "kept" / "root" / "state";
    const mode_t previous = ::umask(0077);
    const auto created = create_private_dirs(state);
    ::umask(previous);
    REQUIRE(created);
    CHECK(mode_of(scratch.path() / "kept") == 0755U);
    CHECK(mode_of(scratch.path() / "kept" / "root") == 0700U);
    CHECK(mode_of(state) == 0700U);
    CHECK(create_private_dirs(state));
}

TEST_CASE("a path through a regular file cannot be created", "[state_locks]") {
    const auto scratch = make_private_scratch("linux-locks");
    write_text(scratch.path() / "file", "x");
    const auto created = create_private_dirs(scratch.path() / "file" / "state");
    REQUIRE_FALSE(created);
}

TEST_CASE("an OFD lock is seen by another open of the same file until it is released", "[state_locks]") {
    const auto scratch = make_private_scratch("linux-locks");
    const NativePath lock_path = scratch.path() / "engine.lock";
    CHECK_FALSE(locked(lock_path));
    {
        auto held = lock_ofd_exclusive(lock_path);
        REQUIRE(held);
        CHECK(mode_of(lock_path) == 0600U);
        CHECK(locked(lock_path));
    }
    CHECK_FALSE(locked(lock_path));
    auto again = lock_ofd_exclusive(lock_path);
    CHECK(again);
}

TEST_CASE("a lock file that cannot be opened fails", "[state_locks]") {
    const auto scratch = make_private_scratch("linux-locks");
    const auto held = lock_ofd_exclusive(scratch.path() / "missing" / "spawn.lock");
    REQUIRE_FALSE(held);
    CHECK(held.error().kind == ErrorKind::NotFound);
}
