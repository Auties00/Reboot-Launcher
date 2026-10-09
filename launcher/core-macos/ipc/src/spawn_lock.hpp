#pragma once

#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/native_path.hpp"
#include "reboot/posix/unique_fd.hpp"

namespace reboot::os_macos::ipc {

// Creates every missing component of the absolute `directory` 0700; existing ones stay as they are.
[[nodiscard]] Result<void> create_private_dirs(const NativePath& directory);

// Opens `path` (created 0600, O_CLOEXEC, O_NOFOLLOW) and waits for flock(LOCK_EX) on it, held
// until the fd closes.
[[nodiscard]] Result<posix::UniqueFd> lock_exclusive(const NativePath& path);

}  // namespace reboot::os_macos::ipc
