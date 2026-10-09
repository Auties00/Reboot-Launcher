#pragma once

#include <vector>

#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/native_path.hpp"
#include "reboot/ports/file_system.hpp"

namespace reboot::os_windows::platform {

// Covers no capability ids; IDiskInfo over the drive-letter APIs.
class WindowsDiskInfo final : public ports::IDiskInfo {
public:
    // Drives that cannot be read, such as an empty card reader or an offline mapped share, are skipped.
    Result<std::vector<ports::VolumeInfo>> volumes() override;
    // Through GetVolumePathNameW, so a volume mounted in a folder is found.
    Result<ports::VolumeInfo> volume_of(const NativePath& path) override;
};

}  // namespace reboot::os_windows::platform
