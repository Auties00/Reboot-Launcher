#include "reboot/testing/fake_disk_info.hpp"

#include <cstddef>
#include <iterator>
#include <mutex>
#include <utility>
#include <vector>

#include "messages.hpp"

namespace reboot::testing {

Result<std::vector<ports::VolumeInfo>> FakeDiskInfo::volumes() {
    if (auto error = faults_.take(DiskOperation::Volumes)) return std::unexpected(std::move(*error));
    const std::scoped_lock lock(mutex_);
    return volumes_;
}

Result<ports::VolumeInfo> FakeDiskInfo::volume_of(const NativePath& path) {
    if (auto error = faults_.take(DiskOperation::VolumeOf)) return std::unexpected(std::move(*error));
    const std::scoped_lock lock(mutex_);
    const NativePath normal = path.lexically_normal();
    const ports::VolumeInfo* best = nullptr;
    std::ptrdiff_t best_depth = 0;
    for (const ports::VolumeInfo& volume : volumes_) {
        const NativePath relative = normal.lexically_relative(volume.mount.lexically_normal());
        if (relative.empty() || *relative.begin() == "..") continue;
        const std::ptrdiff_t depth = std::distance(volume.mount.begin(), volume.mount.end());
        if (best == nullptr || depth > best_depth) {
            best = &volume;
            best_depth = depth;
        }
    }
    if (best == nullptr)
        return make_diag(kTestingDomain, msg::kNoVolume).arg("path", path).kind(ErrorKind::NotFound).fail();
    return *best;
}

void FakeDiskInfo::add_volume(ports::VolumeInfo volume) {
    const std::scoped_lock lock(mutex_);
    volumes_.push_back(std::move(volume));
}

void FakeDiskInfo::set_free_bytes(const NativePath& mount, u64 free_bytes) {
    const std::scoped_lock lock(mutex_);
    for (ports::VolumeInfo& volume : volumes_)
        if (volume.mount == mount) volume.free_bytes = free_bytes;
}

void FakeDiskInfo::clear() {
    const std::scoped_lock lock(mutex_);
    volumes_.clear();
}

}  // namespace reboot::testing
