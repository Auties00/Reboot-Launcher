#include "reboot/os_linux/platform/linux_disk_info.hpp"

#include <cerrno>
#include <filesystem>
#include <map>
#include <string>
#include <sys/statvfs.h>
#include <system_error>
#include <utility>

#include "mount_table.hpp"
#include "reboot/posix/posix_error.hpp"
#include "text_files.hpp"

namespace rb::os_linux::platform {

namespace {

namespace fs = std::filesystem;

const NativePath kMountInfo{"/proc/self/mountinfo"};

// Canonical device path to its label, from udev's /dev/disk/by-label links.
using Labels = std::map<NativePath, std::string>;

[[nodiscard]] Labels read_labels() {
    Labels labels;
    std::error_code error;
    for (fs::directory_iterator it{"/dev/disk/by-label", error}, end; !error && it != end; it.increment(error)) {
        std::error_code resolve_error;
        const NativePath device = fs::canonical(it->path(), resolve_error);
        if (!resolve_error) labels.emplace(device, unescape_hex(it->path().filename().native()));
    }
    return labels;
}

[[nodiscard]] std::optional<NativePath> source_device(const MountEntry& entry) {
    if (!entry.source.starts_with("/dev/")) return std::nullopt;
    std::error_code error;
    NativePath device = fs::canonical(NativePath{entry.source}, error);
    if (error) return std::nullopt;
    return device;
}

// A partition has no removable flag of its own; its disk, the parent in sysfs, has.
[[nodiscard]] bool removable(const NativePath& device) {
    const NativePath block = NativePath{"/sys/class/block"} / device.filename();
    for (const NativePath& flag : {block / "removable", block / ".." / "removable"}) {
        if (const std::optional<std::string> text = try_read_text_file(flag)) return text->starts_with('1');
    }
    return false;
}

[[nodiscard]] Result<ports::VolumeInfo> describe(const MountEntry& entry, const Labels& labels) {
    struct statvfs stats {};
    while (::statvfs(entry.mount_point.c_str(), &stats) != 0) {
        if (errno != EINTR) return std::unexpected(posix::call_failed("statvfs", errno, entry.mount_point));
    }
    ports::VolumeInfo volume;
    volume.mount = entry.mount_point;
    volume.fs_type = entry.fs_type;
    volume.free_bytes = static_cast<u64>(stats.f_bavail) * static_cast<u64>(stats.f_frsize);
    volume.total_bytes = static_cast<u64>(stats.f_blocks) * static_cast<u64>(stats.f_frsize);
    volume.read_only = entry.read_only;
    volume.network = is_network_fs(entry.fs_type);
    if (const std::optional<NativePath> device = source_device(entry)) {
        volume.removable = removable(*device);
        if (const auto label = labels.find(*device); label != labels.end()) volume.label = label->second;
    }
    return volume;
}

}  // namespace

Result<std::vector<ports::VolumeInfo>> LinuxDiskInfo::volumes() {
    Result<std::string> text = read_text_file(kMountInfo);
    if (!text) return std::unexpected(std::move(text.error()));
    const std::vector<MountEntry> entries = parse_mountinfo(*text);
    const Labels labels = read_labels();
    std::vector<ports::VolumeInfo> volumes;
    for (const std::size_t index : real_mounts(entries, true)) {
        Result<ports::VolumeInfo> volume = describe(entries[index], labels);
        if (volume) volumes.push_back(std::move(*volume));
    }
    return volumes;
}

Result<ports::VolumeInfo> LinuxDiskInfo::volume_of(const NativePath& path) {
    std::error_code error;
    const NativePath absolute = fs::absolute(path, error);
    if (error) return std::unexpected(posix::call_failed("getcwd", error.value(), path));
    const NativePath canonical = fs::weakly_canonical(absolute, error);
    if (error) return std::unexpected(posix::call_failed("realpath", error.value(), path));
    Result<std::string> text = read_text_file(kMountInfo);
    if (!text) return std::unexpected(std::move(text.error()));
    const std::vector<MountEntry> entries = parse_mountinfo(*text);
    const std::optional<std::size_t> index = containing_mount(entries, real_mounts(entries, false), canonical);
    if (!index) return std::unexpected(posix::call_failed("statvfs", ENOENT, canonical));
    return describe(entries[*index], read_labels());
}

}  // namespace rb::os_linux::platform
