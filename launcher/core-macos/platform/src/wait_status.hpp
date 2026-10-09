#pragma once

#include "reboot/ports/process.hpp"

namespace rb::os_macos::platform {

// The BSD encoding of a waitpid status, decoded without <sys/wait.h>'s macros, which cast.
[[nodiscard]] ports::ChildExit decode_wait_status(int status) noexcept;

}  // namespace rb::os_macos::platform
