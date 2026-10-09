#include <catch2/catch_test_macros.hpp>

#include <string>
#include <utility>

#include "volume_rules.hpp"

using namespace rb::os_macos::platform;

namespace {

MountFacts mount(std::string path, std::string type) {
    return MountFacts{.mount = std::move(path),
                      .fs_type = std::move(type),
                      .read_only = false,
                      .local = true,
                      .dont_browse = false,
                      .block_size = 4096,
                      .blocks = 1000,
                      .available_blocks = 100};
}

}  // namespace

TEST_CASE("hidden mounts, devfs and autofs are not listed", "[volume_rules]") {
    CHECK(is_listed_mount(mount("/", "apfs")));
    CHECK(is_listed_mount(mount("/Volumes/USB", "msdos")));
    CHECK_FALSE(is_listed_mount(mount("/dev", "devfs")));
    CHECK_FALSE(is_listed_mount(mount("/System/Volumes/Data/home", "autofs")));
    MountFacts data = mount("/System/Volumes/Data", "apfs");
    data.dont_browse = true;
    CHECK_FALSE(is_listed_mount(data));
}

TEST_CASE("the Data volume is presented as /", "[volume_rules]") {
    CHECK(presented_mount("/System/Volumes/Data") == "/");
    CHECK(presented_mount("/Volumes/USB") == "/Volumes/USB");
}

TEST_CASE("a sealed / takes its space and writability from the Data volume", "[volume_rules]") {
    MountFacts root = mount("/", "apfs");
    root.read_only = true;
    MountFacts data = mount("/System/Volumes/Data", "apfs");
    data.available_blocks = 500;
    const auto volume = make_volume(root, data, std::nullopt);
    CHECK(volume.mount == "/");
    CHECK(volume.free_bytes == 500u * 4096u);
    CHECK(volume.total_bytes == 1000u * 4096u);
    CHECK_FALSE(volume.read_only);
    CHECK(volume.label == "/");
}

TEST_CASE("purgeable space counts as free but never shrinks it", "[volume_rules]") {
    const MountFacts usb = mount("/Volumes/USB", "apfs");
    const auto more = make_volume(
        usb, usb, shims::VolumeKeys{.localized_name = "USB", .important_free_bytes = 900u * 4096u, .removable = true, .local = true});
    CHECK(more.free_bytes == 900u * 4096u);
    CHECK(more.label == "USB");
    CHECK(more.removable);
    const auto less = make_volume(
        usb, usb, shims::VolumeKeys{.localized_name = "", .important_free_bytes = 1u, .removable = false, .local = true});
    CHECK(less.free_bytes == 100u * 4096u);
    CHECK(less.label == "USB");
}

TEST_CASE("free space never exceeds the total", "[volume_rules]") {
    const MountFacts disk = mount("/Volumes/D", "apfs");
    const auto volume = make_volume(
        disk, disk,
        shims::VolumeKeys{.localized_name = "D", .important_free_bytes = 5000u * 4096u, .removable = false, .local = true});
    CHECK(volume.free_bytes == volume.total_bytes);
}

TEST_CASE("network shares are flagged from the mount or the volume keys", "[volume_rules]") {
    CHECK(make_volume(mount("/Volumes/share", "smbfs"), mount("/Volumes/share", "smbfs"), std::nullopt).network);
    MountFacts remote = mount("/Volumes/x", "apfs");
    remote.local = false;
    CHECK(make_volume(remote, remote, std::nullopt).network);
    const MountFacts local = mount("/Volumes/y", "apfs");
    CHECK(make_volume(local, local,
                      shims::VolumeKeys{.localized_name = "y", .important_free_bytes = std::nullopt, .removable = false, .local = false})
              .network);
    CHECK_FALSE(make_volume(local, local, std::nullopt).network);
}
