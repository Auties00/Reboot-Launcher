#include "volume_rules.hpp"

#include <array>

namespace rb::os_macos::platform {

namespace {

constexpr std::array<std::string_view, 6> kNetworkTypes{"smbfs", "nfs", "afpfs", "webdav", "ftp", "cifs"};

[[nodiscard]] bool is_network_type(std::string_view fs_type) {
    for (const std::string_view type : kNetworkTypes)
        if (fs_type == type) return true;
    return false;
}

}  // namespace

bool is_listed_mount(const MountFacts& mount) {
    if (mount.dont_browse) return false;
    return mount.fs_type != "devfs" && mount.fs_type != "autofs";
}

std::string presented_mount(std::string_view mount) {
    if (mount == kDataVolume) return "/";
    return std::string(mount);
}

ports::VolumeInfo make_volume(const MountFacts& shown, const MountFacts& space,
                              const std::optional<shims::VolumeKeys>& keys) {
    ports::VolumeInfo volume;
    volume.mount = NativePath{shown.mount};
    volume.fs_type = space.fs_type;
    volume.total_bytes = space.blocks * space.block_size;
    volume.free_bytes = space.available_blocks * space.block_size;
    volume.read_only = space.read_only;
    volume.network = !shown.local || is_network_type(shown.fs_type);
    if (keys) {
        volume.label = keys->localized_name;
        // Purgeable space counts as free; it is never below what f_bavail reports.
        if (keys->important_free_bytes && *keys->important_free_bytes > volume.free_bytes)
            volume.free_bytes = *keys->important_free_bytes;
        volume.removable = keys->removable;
        volume.network = volume.network || !keys->local;
    }
    if (volume.free_bytes > volume.total_bytes) volume.free_bytes = volume.total_bytes;
    if (volume.label.empty()) volume.label = volume.mount == "/" ? std::string("/") : volume.mount.filename().string();
    return volume;
}

}  // namespace rb::os_macos::platform
