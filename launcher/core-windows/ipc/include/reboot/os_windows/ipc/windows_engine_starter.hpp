#pragma once

#include <chrono>
#include <string>
#include <utility>

#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/native_path.hpp"
#include "reboot/foundation/paths.hpp"
#include "reboot/ports/ipc.hpp"

namespace rb::os_windows::ipc {

// Covers no capability ids; IEngineStarter for reboot_client's Autostart mode.
class WindowsEngineStarter final : public ports::IEngineStarter {
public:
    // Longer than everything a holder does under spawn.lock, so only a stuck holder times out.
    static constexpr std::chrono::seconds kSpawnLockWait{8};
    // A hung Schedule service gets its pending call cancelled (CoCancelCall) past this.
    static constexpr std::chrono::seconds kTaskSchedulerDeadline{3};
    // spawn.lock is kept until the started engine's pipe exists, so the next holder finds it.
    static constexpr std::chrono::seconds kEngineReadyWait{4};

    // `caller` is the context captured once at rb_ctx_create.
    WindowsEngineStarter(std::string user_sid, ports::CallerContext caller)
        : user_sid_(std::move(user_sid)), caller_(std::move(caller)) {}

    // ElevatedRefused or NoInteractiveSession before anything starts. Then, under spawn.lock (its
    // missing directories created; platform.spawn_lock_timed_out past kSpawnLockWait), an existing
    // pipe is AlreadyRunning; engine.lock is not probed, since Windows can only test it by taking it.
    // 1. CreateProcessW, detached and breaking away from any job, with engine_environment_block.
    //    ERROR_ACCESS_DENIED (no breakaway) moves to 2; other failures are platform.engine_spawn_failed.
    // 2. Default root only: the on-demand task engine_task_name(sid), if it runs `engine_exe`
    //    least-privileged; AlreadyRunning while an instance runs, else RunEx in the caller's session.
    // 3. CannotDetach. Explorer's IShellDispatch2 is not tried; the autostart resolution allows that.
    // The step-2 MTA thread catches everything and reports internal.bug.
    Result<ports::StartResult> ensure_started(const NativePath& engine_exe, const DataRoot& root) override;

private:
    std::string user_sid_;
    ports::CallerContext caller_;
};

}  // namespace rb::os_windows::ipc
