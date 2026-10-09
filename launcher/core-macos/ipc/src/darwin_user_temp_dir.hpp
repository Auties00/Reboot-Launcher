#pragma once

#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/native_path.hpp"

namespace rb::os_macos::ipc {

// confstr(_CS_DARWIN_USER_TEMP_DIR): the per-user /var/folders/.../T/, the same in every login
// session of the user. The App Sandbox would substitute a container path, so neither the app nor
// the engine is sandboxed. Failure: platform.user_temp_dir_unavailable.
[[nodiscard]] Result<NativePath> darwin_user_temp_dir();

}  // namespace rb::os_macos::ipc
