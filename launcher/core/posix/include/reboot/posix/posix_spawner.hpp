#pragma once

#include <chrono>
#include <optional>
#include <utility>

#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/function.hpp"
#include "reboot/foundation/types.hpp"
#include "reboot/ports/process.hpp"
#include "reboot/posix/unique_fd.hpp"

namespace rb::posix {

// The parent's ends of the child's pipes; the OS launcher watches them and the exit.
struct SpawnedChild {
    u32 pid = 0;
    std::chrono::system_clock::time_point created;
    // Always present. Our own programs (backend, game server) treat EOF on stdin as stop; Wine
    // and other third-party children ignore it, so the OS launcher adds its own guard.
    UniqueFd stdin_write;
    // Present for StdioMode::ControlChannel and StdioMode::Capture.
    UniqueFd stdout_read;
    UniqueFd stderr_read;
};

// Covers no capability ids; the spawn and kill half of the macOS PosixSpawnLauncher. Built on
// macOS only: Linux spawns through clone3 in its own launcher.
class PosixSpawner {
public:
    // The kernel's start time of `pid`; nullopt when no such process exists. A child that already
    // exited but is not yet reaped must still answer (sysctl KERN_PROC; proc_pidinfo fails for a
    // zombie), or spawn would kill and report a child that only ended quickly.
    using StartTimeReader = UniqueFunction<Result<std::optional<std::chrono::system_clock::time_point>>(u32 pid)>;

    explicit PosixSpawner(StartTimeReader read_start_time) noexcept : read_start_time_(std::move(read_start_time)) {}

    // posix_spawn only, never fork/exec (process-model rule 3), with POSIX_SPAWN_CLOEXEC_DEFAULT so
    // the child holds only its stdio, an envp built only from launch.env, and launch.cwd through
    // addchdir_np. The child leads its own process group unless launch.own_group is false, and gets
    // an empty signal mask with default dispositions, since the engine ignores SIGPIPE. `created`
    // comes from the StartTimeReader; when it cannot be read the child is killed and reaped.
    [[nodiscard]] Result<SpawnedChild> spawn(const ports::ProcessLaunch& launch);
    // As spawn, but the child joins the existing group `pgid` and launch.own_group is ignored;
    // the launcher places its watchdog this way.
    [[nodiscard]] Result<SpawnedChild> spawn_into_group(const ports::ProcessLaunch& launch, u32 pgid);

    // `created` must be the recorded start time (same_start_time), which guards against pid reuse.
    [[nodiscard]] Result<bool> is_alive(u32 pid, std::chrono::system_clock::time_point created);
    // Only for native children (the backend and our game server) and for reaping their orphans
    // from runtime.json. A Wine or winhost session never comes here: it stops through winhost's
    // EOF and TerminateJobObject (process-model rule 5). SIGKILL to the group when `pid` leads
    // one, else to `pid` alone; a process already gone is success.
    [[nodiscard]] Result<void> kill_tree(u32 pid, std::chrono::system_clock::time_point created);

private:
    [[nodiscard]] Result<SpawnedChild> spawn_with_group(const ports::ProcessLaunch& launch,
                                                        std::optional<u32> pgid);

    StartTimeReader read_start_time_;
};

}  // namespace rb::posix
