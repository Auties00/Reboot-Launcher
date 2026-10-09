#pragma once

#include <optional>
#include <string>
#include <string_view>

#include "apple_shims.hpp"
#include "reboot/foundation/types.hpp"
#include "reboot/ports/file_system.hpp"

namespace rb::os_macos::platform {

// Where the sealed system volume keeps user files.
inline constexpr std::string_view kDataVolume = "/System/Volumes/Data";

// One struct statfs, reduced to what IDiskInfo reports.
struct MountFacts {
    std::string mount;
    std::string fs_type;
    bool read_only = false;
    // MNT_LOCAL.
    bool local = true;
    // MNT_DONTBROWSE: system volumes (Preboot, VM, Data) that Finder hides.
    bool dont_browse = false;
    u64 block_size = 0;
    u64 blocks = 0;
    u64 available_blocks = 0;
};

// Finder's view: browsable mounts, without devfs and the automounter's trigger points.
[[nodiscard]] bool is_listed_mount(const MountFacts& mount);

// Files on the Data volume belong to "/" as the user sees it.
[[nodiscard]] std::string presented_mount(std::string_view mount);

// `shown` names the volume; `space` is where its free space and writability come from, which for
// a sealed "/" is the Data volume. `keys` are the NSURL volume keys of `shown`, when readable.
[[nodiscard]] ports::VolumeInfo make_volume(const MountFacts& shown, const MountFacts& space,
                                            const std::optional<shims::VolumeKeys>& keys);

}  // namespace rb::os_macos::platform
