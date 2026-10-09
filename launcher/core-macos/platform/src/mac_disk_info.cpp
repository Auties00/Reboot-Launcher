#include "darwin.hpp"

#include "reboot/os_macos/platform/mac_disk_info.hpp"

#include <sys/mount.h>
#include <sys/param.h>

#include <cerrno>
#include <cstring>
#include <string>

#include "apple_shims.hpp"
#include "reboot/posix/posix_error.hpp"
#include "volume_rules.hpp"

namespace rb::os_macos::platform {

namespace {

[[nodiscard]] MountFacts facts_of(const struct statfs& stats) {
    return MountFacts{.mount = std::string(stats.f_mntonname, ::strnlen(stats.f_mntonname, sizeof stats.f_mntonname)),
                      .fs_type = std::string(stats.f_fstypename, ::strnlen(stats.f_fstypename, sizeof stats.f_fstypename)),
                      .read_only = (stats.f_flags & MNT_RDONLY) != 0,
                      .local = (stats.f_flags & MNT_LOCAL) != 0,
                      .dont_browse = (stats.f_flags & MNT_DONTBROWSE) != 0,
                      .block_size = static_cast<u64>(stats.f_bsize),
                      .blocks = static_cast<u64>(stats.f_blocks),
                      .available_blocks = static_cast<u64>(stats.f_bavail)};
}

[[nodiscard]] Result<MountFacts> stat_mount(const NativePath& path) {
    struct statfs stats {};
    while (::statfs(path.c_str(), &stats) != 0) {
        if (errno != EINTR) return std::unexpected(posix::call_failed("statfs", errno, path));
    }
    return facts_of(stats);
}

// A sealed, read-only "/" stands for the Data volume, which holds every user file.
[[nodiscard]] ports::VolumeInfo describe(const MountFacts& mount) {
    MountFacts space = mount;
    if (mount.mount == "/" && mount.read_only) {
        if (Result<MountFacts> data = stat_mount(NativePath{std::string(kDataVolume)}); data && data->mount == kDataVolume)
            space = *data;
    }
    return make_volume(mount, space, shims::volume_keys(NativePath{mount.mount}));
}

}  // namespace

Result<std::vector<ports::VolumeInfo>> MacDiskInfo::volumes() {
    std::vector<struct statfs> mounts;
    for (;;) {
        const int count = ::getfsstat(nullptr, 0, MNT_NOWAIT);
        if (count < 0) return std::unexpected(posix::call_failed("getfsstat", errno));
        // Room for a volume mounted between the two calls.
        mounts.resize(static_cast<std::size_t>(count) + 4);
        const int got = ::getfsstat(mounts.data(), static_cast<int>(mounts.size() * sizeof(struct statfs)), MNT_NOWAIT);
        if (got < 0) return std::unexpected(posix::call_failed("getfsstat", errno));
        if (static_cast<std::size_t>(got) < mounts.size()) {
            mounts.resize(static_cast<std::size_t>(got));
            break;
        }
    }
    std::vector<ports::VolumeInfo> volumes;
    for (const struct statfs& stats : mounts) {
        const MountFacts mount = facts_of(stats);
        if (is_listed_mount(mount)) volumes.push_back(describe(mount));
    }
    return volumes;
}

Result<ports::VolumeInfo> MacDiskInfo::volume_of(const NativePath& path) {
    // A path that does not exist yet lives on the volume of its nearest existing ancestor.
    NativePath probe = path.lexically_normal();
    Result<MountFacts> mount = stat_mount(probe);
    while (!mount && mount.error().kind == ErrorKind::NotFound && probe.has_relative_path()) {
        probe = probe.parent_path();
        mount = stat_mount(probe);
    }
    if (!mount) return std::unexpected(std::move(mount.error()));
    const std::string shown = presented_mount(mount->mount);
    if (shown != mount->mount) {
        Result<MountFacts> presented = stat_mount(NativePath{shown});
        if (!presented) return std::unexpected(std::move(presented.error()));
        return describe(*presented);
    }
    return describe(*mount);
}

}  // namespace rb::os_macos::platform
