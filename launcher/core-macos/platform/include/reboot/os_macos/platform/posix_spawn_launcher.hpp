#pragma once

#include <chrono>
#include <memory>

#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/types.hpp"
#include "reboot/ports/process.hpp"

namespace reboot::os_macos::platform {

// Covers no capability ids; IProcessLauncher over posix::PosixSpawner, with a watchdog per child.
class PosixSpawnLauncher final : public ports::IProcessLauncher {
public:
    PosixSpawnLauncher();
    ~PosixSpawnLauncher() override;
    PosixSpawnLauncher(const PosixSpawnLauncher&) = delete;
    PosixSpawnLauncher& operator=(const PosixSpawnLauncher&) = delete;

    // The watchdog kills the child's group, or its pid, once the engine's O_CLOEXEC pipe to it closes.
    // So an in-place update's execve kills every remaining child, the backend included.
    // A pid watchdog fires before the child is reaped, so it never hits a reused pid.
    // Callbacks run on the launcher's kqueue thread; on_exit follows the last output.
    Result<std::unique_ptr<ports::ChildProcess>> spawn(const ports::ProcessLaunch& launch) override;
    Result<bool> is_alive(u32 pid, std::chrono::system_clock::time_point created) override;
    // The group when `pid` leads one, else `pid` alone; a process already gone is success.
    Result<void> kill(u32 pid, std::chrono::system_clock::time_point created) override;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace reboot::os_macos::platform
