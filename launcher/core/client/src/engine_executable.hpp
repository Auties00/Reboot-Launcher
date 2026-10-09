#pragma once

#include "reboot/foundation/native_path.hpp"
#include "reboot/ports/platform_paths.hpp"

namespace rb::client {

// reboot-engine (reboot-engine.exe on Windows) in IPlatformPaths::exe_dir().
[[nodiscard]] NativePath engine_executable(const ports::IPlatformPaths& paths);

}  // namespace rb::client
