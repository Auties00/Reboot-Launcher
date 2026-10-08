#pragma once

#include <string>
#include <string_view>

#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/native_path.hpp"
#include "reboot/ports/os_services.hpp"

namespace reboot::os_windows::platform {

// Task Scheduler names are machine-wide, hence the SID. Must equal core-windows/ipc's copy.
[[nodiscard]] std::string engine_task_name(std::string_view user_sid);

// Covers no capability ids; IIntegrationRegistrar over HKCU and the per-user Task Scheduler.
// Ownership is read from the registered command: Ours when its exe is under `install_root`,
// Stale when that exe is gone, Foreign otherwise.
class WindowsIntegrationRegistrar final : public ports::IIntegrationRegistrar {
public:
    WindowsIntegrationRegistrar(NativePath install_root, std::string user_sid);

    Result<ports::IntegrationStatus> status(ports::IntegrationKind kind) override;
    // Overwrites any entry: IntegrationService decides when a Foreign one may be replaced.
    Result<void> apply(ports::IntegrationKind kind, const NativePath& exe) override;
    // Leaves a Foreign entry in place.
    Result<void> remove(ports::IntegrationKind kind) override;

private:
    NativePath install_root_;
    std::string user_sid_;
};

}  // namespace reboot::os_windows::platform
