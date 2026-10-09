#pragma once

#include <chrono>
#include <span>
#include <string>

#include "engine_start_rules.hpp"
#include "reboot/foundation/diag.hpp"

namespace reboot::os_macos::ipc {

// posix_spawn of /bin/launchctl with `arguments`, an empty environment, stdio on /dev/null, no
// other fd, an empty signal mask and default dispositions. It starts suspended and is resumed
// only once kqueue watches it (EVFILT_PROC, NOTE_EXIT|NOTE_EXITSTATUS), so its status arrives
// even when the host ignores SIGCHLD. Past `deadline` it is SIGKILLed and reaped: TimedOut.
// Fails only when it cannot be started or watched.
[[nodiscard]] Result<LaunchctlRun> run_launchctl(std::span<const std::string> arguments,
                                                 std::chrono::milliseconds deadline);

}  // namespace reboot::os_macos::ipc
