#pragma once

#include <chrono>
#include <utility>

#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/native_path.hpp"
#include "unique_handle.hpp"

namespace rb::os_windows::ipc {

// An exclusive LockFileEx on state/spawn.lock, released when destroyed.
class SpawnLock {
public:
    // Creates the missing directories first. platform.spawn_lock_timed_out past `wait`; a
    // failing call is platform.ipc_call_failed_on_path.
    [[nodiscard]] static Result<SpawnLock> acquire(const NativePath& path, std::chrono::milliseconds wait);

private:
    explicit SpawnLock(UniqueHandle file) : file_(std::move(file)) {}

    UniqueHandle file_;
};

}  // namespace rb::os_windows::ipc
