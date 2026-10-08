#pragma once

#include <vector>

#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/native_path.hpp"
#include "reboot/ports/file_system.hpp"

namespace reboot::os_macos::platform {

// Covers no capability ids; IDiskInfo over statfs plus NSURL volume resource keys.
class MacDiskInfo final : public ports::IDiskInfo {
public:
    // The sealed "/" reports the free space of /System/Volumes/Data, where user files live.
    Result<std::vector<ports::VolumeInfo>> volumes() override;
    // Free space counts purgeable APFS space, which f_bavail leaves out.
    Result<ports::VolumeInfo> volume_of(const NativePath& path) override;
};

}  // namespace reboot::os_macos::platform
