#pragma once

#include <vector>

#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/native_path.hpp"
#include "reboot/ports/file_system.hpp"

namespace reboot::os_linux::platform {

// Covers no capability ids; IDiskInfo over /proc/self/mountinfo and statvfs.
class LinuxDiskInfo final : public ports::IDiskInfo {
public:
    // One entry per mount of a block device or network file system; pseudo file systems (proc,
    // sysfs, cgroup, tmpfs, squashfs and the like) are skipped. A bind mount is one whose source
    // an earlier mount already shows at an ancestor root; btrfs subvolumes (Fedora's /root and
    // /home) have disjoint roots and stay. Sizes are statvfs f_bavail; read_only is the ro
    // option; removable comes from the source device's /sys/class/block/<dev>/removable (or its
    // disk's), since btrfs reports anonymous 0:N numbers. network covers nfs, nfs4, cifs, smb3
    // and fuse.sshfs; the label comes from /dev/disk/by-label. A mount statvfs cannot reach is skipped.
    Result<std::vector<ports::VolumeInfo>> volumes() override;
    // The mount with the longest mount point containing the canonical `path`, so bind mounts
    // and the Steam Deck's microSD under /run/media resolve to their own volume.
    Result<ports::VolumeInfo> volume_of(const NativePath& path) override;
};

}  // namespace reboot::os_linux::platform
