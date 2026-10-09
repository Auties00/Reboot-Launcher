#pragma once

#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/native_path.hpp"
#include "reboot/posix/unique_fd.hpp"

namespace reboot::os_linux::ipc {

// Creates every missing component of the absolute `directory` 0700; existing ones stay as they are.
[[nodiscard]] Result<void> create_private_dirs(const NativePath& directory);

// Opens `path` (created 0600, O_CLOEXEC) and waits for an OFD write lock on it, held until the
// fd closes.
[[nodiscard]] Result<posix::UniqueFd> lock_ofd_exclusive(const NativePath& path);

// Whether another open file description holds an OFD or POSIX lock on `path` (F_OFD_GETLK, which
// flock locks escape); false when the file is missing.
[[nodiscard]] Result<bool> is_ofd_locked(const NativePath& path);

}  // namespace reboot::os_linux::ipc
