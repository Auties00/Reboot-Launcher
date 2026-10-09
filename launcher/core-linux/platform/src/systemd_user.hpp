#pragma once

#include <string>
#include <vector>

#include "helper_process.hpp"
#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/native_path.hpp"

namespace reboot::os_linux::platform {

// Whether something accepts connections on the AF_UNIX stream socket at `path`.
[[nodiscard]] bool unix_socket_accepts(const NativePath& path);

// Whether a systemd user manager answers at <runtime_dir>/systemd/private.
[[nodiscard]] bool systemd_user_manager_answers(const NativePath& runtime_dir);

// `systemctl --user <args>` with this process's environment and XDG_RUNTIME_DIR, which it needs
// to reach the user manager, set to `runtime_dir`; bounded by kHelperDeadline.
[[nodiscard]] Result<HelperResult> systemctl_user(std::vector<std::string> args, const NativePath& runtime_dir);

// systemctl_user, failing with platform.helper_failed unless it exits 0.
[[nodiscard]] Result<void> systemctl_user_ok(std::vector<std::string> args, const NativePath& runtime_dir);

}  // namespace reboot::os_linux::platform
