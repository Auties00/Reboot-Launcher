#pragma once

#include <chrono>
#include <functional>
#include <string>
#include <string_view>

#include "reboot/base/diag.hpp"
#include "reboot/base/native_path.hpp"
#include "reboot/base/types.hpp"

namespace rb::os_windows::ipc {

enum class TaskRun : u8 {
    // Absent, disabled, not running `engine_exe` least-privileged, or the Task Scheduler failed.
    Unusable,
    // An instance already runs.
    Running,
    // RunEx accepted it.
    Started,
};

// Runs on the task thread as each Task Scheduler call begins, inside its deadline; the conformance
// suite hangs a call with it.
using TaskCallHook = std::function<void(std::string_view call)>;

// The on-demand engine task `task_name` in the root folder, run on an MTA thread of its own and
// started in `session_id`. A call still pending past `call_deadline` is cancelled with
// CoCancelCall and fails with platform.task_scheduler_timed_out, at most a second later even when
// the call ignores the cancel; no later call starts. Whatever the thread throws is internal.bug.
// Other failures are logged and Unusable.
[[nodiscard]] Result<TaskRun> run_engine_task(const std::string& task_name, const NativePath& engine_exe, u32 session_id,
                                              std::chrono::milliseconds call_deadline, TaskCallHook before_call = {});

}  // namespace rb::os_windows::ipc
