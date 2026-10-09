#pragma once

#include <chrono>
#include <optional>
#include <string>
#include <vector>

#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/types.hpp"

namespace reboot::os_macos::platform {

// sysctl KERN_PROC_PID, which still answers for an exited child nobody reaped yet; nullopt when no
// such process exists.
[[nodiscard]] Result<std::optional<std::chrono::system_clock::time_point>> process_start_time(u32 pid);
// p_comm, the executable name truncated to MAXCOMLEN; also readable for an unreaped child.
[[nodiscard]] std::optional<std::string> process_command_name(u32 pid);
// Whether our child `pid` exited, without reaping it; true once it is no child of ours.
[[nodiscard]] bool has_exited(u32 pid) noexcept;
// The children of `parent`, zombies included.
[[nodiscard]] Result<std::vector<u32>> child_pids(u32 parent);

}  // namespace reboot::os_macos::platform
