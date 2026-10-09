#pragma once

#include "reboot/foundation/diag.hpp"

namespace rb::os_macos::platform {

// fcntl(F_FULLFSYNC), or fsync where the file system rejects it (SMB, exFAT).
[[nodiscard]] Result<void> full_fsync(int fd);

}  // namespace rb::os_macos::platform
