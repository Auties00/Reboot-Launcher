#include <catch2/catch_test_macros.hpp>

#include <string_view>

#include "fs_event_change.hpp"

using namespace reboot::os_macos::platform;
using reboot::NativePath;
using reboot::ports::FileChangeKind;

namespace {

const NativePath kWatched{"/var/folders/x/T/watch"};
const NativePath kReal{"/private/var/folders/x/T/watch"};

}  // namespace

TEST_CASE("a direct entry is reported under the spelling the caller watched", "[fs_event_change]") {
    const auto change = fs_event_change(kWatched, kReal, "/private/var/folders/x/T/watch/a.json", {.created = true}, true);
    REQUIRE(change);
    CHECK(change->path == kWatched / "a.json");
    CHECK(change->kind == FileChangeKind::Created);
}

TEST_CASE("entries of subdirectories and the directory itself are not reported", "[fs_event_change]") {
    CHECK_FALSE(fs_event_change(kWatched, kReal, "/private/var/folders/x/T/watch/sub/a.json", {.modified = true}, true));
    CHECK_FALSE(fs_event_change(kWatched, kReal, "/private/var/folders/x/T/watch", {.modified = true}, true));
    CHECK_FALSE(fs_event_change(kWatched, kReal, "/private/var/folders/x/T/other/a.json", {.created = true}, true));
}

TEST_CASE("lost events report Modified on the directory, asking for a rescan", "[fs_event_change]") {
    const auto change = fs_event_change(kWatched, kReal, "/", {.dropped = true}, false);
    REQUIRE(change);
    CHECK(change->path == kWatched);
    CHECK(change->kind == FileChangeKind::Modified);
}

TEST_CASE("coalesced flags pick one kind", "[fs_event_change]") {
    const std::string_view path = "/private/var/folders/x/T/watch/a.json";
    CHECK(fs_event_change(kWatched, kReal, path, {.created = true, .removed = true, .renamed = true}, true)->kind ==
          FileChangeKind::Renamed);
    CHECK(fs_event_change(kWatched, kReal, path, {.created = true, .removed = true}, false)->kind ==
          FileChangeKind::Removed);
    CHECK(fs_event_change(kWatched, kReal, path, {.created = true, .removed = true}, true)->kind ==
          FileChangeKind::Created);
    CHECK(fs_event_change(kWatched, kReal, path, {.removed = true}, true)->kind == FileChangeKind::Modified);
    CHECK(fs_event_change(kWatched, kReal, path, {.modified = true}, true)->kind == FileChangeKind::Modified);
    CHECK_FALSE(fs_event_change(kWatched, kReal, path, {}, true));
}

TEST_CASE("a trailing separator on the event path is ignored", "[fs_event_change]") {
    const auto change = fs_event_change(kWatched, kReal, "/private/var/folders/x/T/watch/dir/", {.created = true}, true);
    REQUIRE(change);
    CHECK(change->path == kWatched / "dir");
}
