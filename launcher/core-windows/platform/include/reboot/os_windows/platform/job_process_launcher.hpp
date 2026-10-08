#pragma once

#include <chrono>
#include <memory>

#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/types.hpp"
#include "reboot/ports/process.hpp"

namespace reboot::os_windows::platform {

// Covers no capability ids; IProcessLauncher for native children (backend, game server).
class JobProcessLauncher final : public ports::IProcessLauncher {
public:
    // launch.env is layered over CreateEnvironmentBlock(engine token), which supplies SystemRoot and TEMP.
    Result<std::unique_ptr<ports::ChildProcess>> spawn(const ports::ProcessLaunch& launch) override;
    // Creation times compare at microseconds, the precision runtime.json keeps.
    Result<bool> is_alive(u32 pid, std::chrono::system_clock::time_point created) override;
    Result<void> kill(u32 pid, std::chrono::system_clock::time_point created) override;
};

}  // namespace reboot::os_windows::platform
