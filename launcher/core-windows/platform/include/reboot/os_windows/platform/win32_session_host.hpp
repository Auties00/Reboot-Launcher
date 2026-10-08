#pragma once

#include <memory>

#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/function.hpp"
#include "reboot/ports/session_host.hpp"

namespace reboot::ports {
class IFileSystem;
}

namespace reboot::os_windows::platform {

// Covers no capability ids (win32session holds them); ISessionHost for native Windows play,
// injecting from the engine process.
class Win32SessionHost final : public ports::ISessionHost {
public:
    explicit Win32SessionHost(ports::IFileSystem& fs) noexcept : fs_(fs) {}

    // Each inject entry stays held deny-write from its hash check until the session ends, so it
    // cannot be swapped before the load. launch.env is layered over CreateEnvironmentBlock, and
    // launch.park goes to win32session as SpawnGame::park_utf16.
    Result<std::unique_ptr<ports::IGameSession>> launch(const ports::SessionLaunch& launch,
                                                        UniqueFunction<void(ports::SessionHostEvent)> on_event) override;

private:
    ports::IFileSystem& fs_;
};

}  // namespace reboot::os_windows::platform
