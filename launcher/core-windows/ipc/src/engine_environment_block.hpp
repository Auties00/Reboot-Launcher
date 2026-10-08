#pragma once

#include <span>
#include <string>

#include "reboot/foundation/paths.hpp"

namespace reboot::os_windows::ipc {

// The CREATE_UNICODE_ENVIRONMENT block for a spawned engine: `inherited` ("NAME=value" entries)
// minus every REBOOT_* name, compared case-insensitively as Windows does, plus
// REBOOT_LAUNCHER_HOME=<root> when the root is overridden.
[[nodiscard]] std::wstring engine_environment_block(std::span<const std::wstring> inherited, const DataRoot& root);

}  // namespace reboot::os_windows::ipc
