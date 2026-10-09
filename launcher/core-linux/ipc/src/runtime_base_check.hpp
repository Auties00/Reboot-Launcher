#pragma once

#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/native_path.hpp"
#include "reboot/foundation/types.hpp"

namespace rb::os_linux::ipc {

// lstat: a real directory owned by `uid` with mode exactly 0700, otherwise ipc.endpoint_untrusted
// caused by posix.not_a_directory or posix.directory_not_private. A failed lstat is its
// posix.call_failed_on_path, ErrorKind::NotFound when the directory is missing.
[[nodiscard]] Result<void> check_runtime_base(const NativePath& runtime_base, u32 uid);

// Creates the directory 0700 when it is missing (its parent must exist), then check_runtime_base.
[[nodiscard]] Result<void> ensure_runtime_base(const NativePath& runtime_base, u32 uid);

}  // namespace rb::os_linux::ipc
