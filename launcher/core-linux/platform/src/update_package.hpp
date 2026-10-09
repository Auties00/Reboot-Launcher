#pragma once

#include <string>

#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/native_path.hpp"

namespace rb::os_linux::platform {

// Extracts the tarball update `package` (tar, optionally zstd) into the empty directory `staging`
// and returns the name of the one directory it must hold (platform.update_package_invalid
// otherwise). Entries that are absolute, hold "..", are links resolving outside `staging`, or are
// devices, FIFOs or sockets fail with platform.update_entry_unsafe; setuid, setgid and sticky
// bits are dropped.
[[nodiscard]] Result<std::string> extract_update(const NativePath& package, const NativePath& staging);

}  // namespace rb::os_linux::platform
