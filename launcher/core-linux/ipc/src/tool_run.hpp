#pragma once

#include <chrono>
#include <optional>
#include <span>
#include <string>

namespace reboot::os_linux::ipc {

struct ToolRun {
    int exit_code = 0;
    std::string output;
};

// posix_spawnp of argv[0] with this process's environment, stdout captured, stdin and stderr on
// /dev/null, and default signal dispositions and an empty mask, since the host app's would be
// inherited. SIGKILLed and reaped past `deadline`. nullopt when it could not start, timed out, was
// killed by a signal, or was reaped elsewhere (a host that ignores SIGCHLD).
[[nodiscard]] std::optional<ToolRun> run_tool(std::span<const std::string> argv, std::chrono::milliseconds deadline);

}  // namespace reboot::os_linux::ipc
