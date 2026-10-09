#pragma once

#include <chrono>
#include <memory>
#include <optional>

#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/native_path.hpp"
#include "reboot/foundation/types.hpp"
#include "reboot/ports/process.hpp"

namespace rb::os_linux::platform {

// The start time of `pid` from field 22 of /proc/<pid>/stat, against the boot time; nullopt
// when no such process exists.
[[nodiscard]] Result<std::optional<std::chrono::system_clock::time_point>> read_proc_start_time(u32 pid);

// Covers no capability ids; IProcessLauncher for native children (backend, game server, runner).
// Amends process-model rule 3 on Linux, as posix::PosixSpawner records: posix_spawn cannot set
// the PR_SET_PDEATHSIG that the IProcessLauncher port requires. Linux 5.3 floor.
class PidfdProcessLauncher final : public ports::IProcessLauncher {
public:
    // Present when a systemd user manager answers at <runtime_dir>/systemd/private.
    struct SystemdScopes {
        NativePath systemd_run;
        // $XDG_RUNTIME_DIR, which systemd-run needs to reach the user manager.
        NativePath runtime_dir;
    };

    explicit PidfdProcessLauncher(std::optional<SystemdScopes> scopes);
    ~PidfdProcessLauncher() override;
    PidfdProcessLauncher(const PidfdProcessLauncher&) = delete;
    PidfdProcessLauncher& operator=(const PidfdProcessLauncher&) = delete;

    // Spawns on one long-lived thread, since PDEATHSIG fires when the creating thread exits.
    // clone3(CLONE_PIDFD | CLONE_VFORK), else clone(CLONE_VFORK) and pidfd_open where seccomp
    // refuses clone3. The child closes fds >= 3 with close_range (5.9), else a close loop up to
    // RLIMIT_NOFILE read before the clone. It takes PDEATHSIG=SIGKILL (exiting if the engine is
    // already gone), leads its own group unless launch.own_group is false, resets its signal
    // mask and dispositions, and gets launch.env alone. With launch.scope_name and scopes,
    // systemd-run --user --scope execs the command in place with runtime_dir added, which
    // `env -u XDG_RUNTIME_DIR` strips again when launch.env lacks it. The scope is the backstop
    // for processes that leave the group (wineserver); terminate_tree kills the group, then stops it.
    // An exited child stays a zombie until its handle is destroyed, so terminate_tree can still
    // kill what is left of its group without hitting a reused pid.
    Result<std::unique_ptr<ports::ChildProcess>> spawn(const ports::ProcessLaunch& launch) override;
    // `created` must equal read_proc_start_time, which guards against pid reuse; a zombie is not alive.
    Result<bool> is_alive(u32 pid, std::chrono::system_clock::time_point created) override;
    // SIGKILL to the process group `pid` leads after the same check; a process already gone is success.
    Result<void> kill(u32 pid, std::chrono::system_clock::time_point created) override;

private:
    // One epoll thread watches pipes and pidfds. As PID 1 (a container entrypoint) it also reaps
    // re-parented orphans on SIGCHLD, peeking with WNOWAIT so our own children keep their status.
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace rb::os_linux::platform
