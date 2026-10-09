#include <catch2/catch_test_macros.hpp>

#include <string_view>
#include <vector>

#include "mount_table.hpp"

using reboot::NativePath;
using namespace reboot::os_linux::platform;

namespace {

// Fedora with btrfs subvolumes, a bind mount of a home subdirectory, tmpfs and a microSD card.
constexpr std::string_view kMountInfo =
    "22 1 0:31 /root / rw,relatime shared:1 - btrfs /dev/nvme0n1p3 rw,ssd,subvol=/root\n"
    "23 22 0:21 / /proc rw,nosuid shared:5 - proc proc rw\n"
    "24 22 0:31 /home /home rw,relatime shared:2 - btrfs /dev/nvme0n1p3 rw,ssd,subvol=/home\n"
    "25 22 0:33 / /tmp rw,nosuid shared:6 - tmpfs tmpfs rw\n"
    "26 24 0:31 /home/ada/games /srv/games ro,relatime shared:2 - btrfs /dev/nvme0n1p3 rw\n"
    "27 22 179:1 / /run/media/deck/SD\\040Card rw,relatime shared:9 master:3 - ext4 /dev/mmcblk0p1 rw\n"
    "28 22 0:40 / /mnt/share rw shared:10 - nfs4 server:/export rw\n"
    "garbage line\n"
    "29 22 8:1 / /mnt/no-separator rw ext4 /dev/sda1 rw\n";

}  // namespace

TEST_CASE("mountinfo lines parse with escapes and optional fields", "[mount_table]") {
    const std::vector<MountEntry> entries = parse_mountinfo(kMountInfo);
    REQUIRE(entries.size() == 7);
    CHECK(entries[0].major == 0);
    CHECK(entries[0].minor == 31);
    CHECK(entries[0].root == "/root");
    CHECK(entries[0].mount_point == NativePath{"/"});
    CHECK(entries[0].fs_type == "btrfs");
    CHECK(entries[0].source == "/dev/nvme0n1p3");
    CHECK_FALSE(entries[0].read_only);
    CHECK(entries[4].read_only);
    CHECK(entries[5].mount_point == NativePath{"/run/media/deck/SD Card"});
    CHECK(entries[5].major == 179);
    CHECK(entries[6].source == "server:/export");
}

TEST_CASE("octal and hex escapes decode", "[mount_table]") {
    CHECK(unescape_octal("a\\040b\\011c\\134d") == "a b\tc\\d");
    CHECK(unescape_octal("trailing\\04") == "trailing\\04");
    CHECK(unescape_hex("My\\x20Disk") == "My Disk");
    CHECK(unescape_hex("bad\\xZZ") == "bad\\xZZ");
}

TEST_CASE("pseudo and network file systems are told apart", "[mount_table]") {
    CHECK(is_pseudo_fs("proc"));
    CHECK(is_pseudo_fs("tmpfs"));
    CHECK(is_pseudo_fs("squashfs"));
    CHECK_FALSE(is_pseudo_fs("ext4"));
    CHECK_FALSE(is_pseudo_fs("overlay"));
    CHECK(is_network_fs("nfs4"));
    CHECK(is_network_fs("fuse.sshfs"));
    CHECK_FALSE(is_network_fs("btrfs"));
}

TEST_CASE("subvolumes with disjoint roots stay, bind mounts and pseudo file systems go", "[mount_table]") {
    const std::vector<MountEntry> entries = parse_mountinfo(kMountInfo);
    const std::vector<std::size_t> volumes = real_mounts(entries, true);
    std::vector<NativePath> mounts;
    for (const std::size_t index : volumes) mounts.push_back(entries[index].mount_point);
    CHECK(mounts == std::vector<NativePath>{"/", "/home", "/run/media/deck/SD Card", "/mnt/share"});

    const std::vector<std::size_t> with_binds = real_mounts(entries, false);
    CHECK(with_binds.size() == 5);
}

TEST_CASE("the longest containing mount point wins", "[mount_table]") {
    const std::vector<MountEntry> entries = parse_mountinfo(kMountInfo);
    const std::vector<std::size_t> candidates = real_mounts(entries, false);
    const auto mount_of = [&](const NativePath& path) {
        const auto index = containing_mount(entries, candidates, path);
        return index ? entries[*index].mount_point : NativePath{};
    };
    CHECK(mount_of("/home/ada/.local/share") == NativePath{"/home"});
    CHECK(mount_of("/srv/games/x") == NativePath{"/srv/games"});
    CHECK(mount_of("/run/media/deck/SD Card/steamapps") == NativePath{"/run/media/deck/SD Card"});
    CHECK(mount_of("/homework") == NativePath{"/"});
    CHECK(mount_of("/tmp/x") == NativePath{"/"});
}
