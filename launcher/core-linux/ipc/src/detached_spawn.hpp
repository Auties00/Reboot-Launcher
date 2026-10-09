#pragma once

#include <string>
#include <vector>

#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/native_path.hpp"

namespace reboot::os_linux::ipc {

struct DetachedLaunch {
    NativePath program;
    // argv[0] included.
    std::vector<std::string> argv;
    // "NAME=value" entries; the whole environment.
    std::vector<std::string> envp;
    NativePath cwd;
};

// A setsid double fork, so the program is reparented away from this process and its session.
// The grandchild resets every signal disposition and the signal mask, closes every fd >= 3,
// puts stdio on /dev/null, enters `cwd` and execs. Everything is built before fork, so the
// children make only async-signal-safe calls. Returns once the exec succeeded or failed: an
// errno travels back over a close-on-exec pipe as platform.ipc_engine_spawn_failed.
[[nodiscard]] Result<void> spawn_detached(const DetachedLaunch& launch);

}  // namespace reboot::os_linux::ipc
