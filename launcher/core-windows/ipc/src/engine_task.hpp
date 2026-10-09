#pragma once

#include <chrono>
#include <string>

#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/native_path.hpp"
#include "reboot/foundation/types.hpp"

namespace rb::os_windows::ipc {

enum class TaskRun : u8 {
    // Absent, disabled, not running `engine_exe` least-privileged, or the Task Scheduler failed.
    Unusable,
    // An instance already runs.
    Running,
    // RunEx accepted it.
    Started,
};

// The on-demand engine task `task_name` in the root folder, run on an MTA thread of its own and
// started in `session_id`. A call still pending past `call_deadline` is cancelled with
// CoCancelCall and fails with platform.task_scheduler_timed_out; whatever the thread throws is
// internal.bug. Other failures are logged and Unusable.
[[nodiscard]] Result<TaskRun> run_engine_task(const std::string& task_name, const NativePath& engine_exe, u32 session_id,
                                              std::chrono::milliseconds call_deadline);

}  // namespace rb::os_windows::ipc
