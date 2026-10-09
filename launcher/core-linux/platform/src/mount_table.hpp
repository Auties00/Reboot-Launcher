#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "reboot/foundation/native_path.hpp"
#include "reboot/foundation/types.hpp"

namespace reboot::os_linux::platform {

// One line of /proc/self/mountinfo, with the kernel's octal escapes (\040 and so on) decoded.
struct MountEntry {
    u32 major = 0;
    u32 minor = 0;
    // The directory of the file system that is mounted, "/" unless a bind mount or subvolume.
    std::string root;
    NativePath mount_point;
    std::string fs_type;
    std::string source;
    bool read_only = false;
};

// Malformed lines are skipped.
[[nodiscard]] std::vector<MountEntry> parse_mountinfo(std::string_view text);

// \040-style escapes as mountinfo writes them.
[[nodiscard]] std::string unescape_octal(std::string_view text);
// \x20-style escapes as udev writes /dev/disk/by-label names.
[[nodiscard]] std::string unescape_hex(std::string_view text);

// proc, sysfs, cgroup, tmpfs, squashfs and the like, which hold no user data.
[[nodiscard]] bool is_pseudo_fs(std::string_view fs_type) noexcept;
// nfs, nfs4, cifs, smb3 and fuse.sshfs.
[[nodiscard]] bool is_network_fs(std::string_view fs_type) noexcept;

// Indexes of the entries that are not pseudo file systems, in order; with `skip_binds`, also not
// bind mounts: a mount whose device an earlier listed mount shows at an ancestor (or the same) root.
[[nodiscard]] std::vector<std::size_t> real_mounts(const std::vector<MountEntry>& entries, bool skip_binds);

// The index in `candidates` of the entry with the longest mount point containing `path`.
[[nodiscard]] std::optional<std::size_t> containing_mount(const std::vector<MountEntry>& entries,
                                                          const std::vector<std::size_t>& candidates,
                                                          const NativePath& path);

}  // namespace reboot::os_linux::platform
