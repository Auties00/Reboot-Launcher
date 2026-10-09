#pragma once

#include <string_view>

#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/native_path.hpp"
#include "reboot/ports/os_services.hpp"

namespace rb::os_windows::platform {

// Covers no capability ids; IShellLauncher over the Explorer shell. Each call runs on a
// short-lived STA thread, since worker threads carry no COM apartment.
class WindowsShell final : public ports::IShellLauncher {
public:
    Result<void> open_url(std::string_view https_url) override;
    Result<void> open_path(const NativePath& path) override;
    Result<void> reveal(const NativePath& path) override;
    // Fails rather than deleting permanently when the volume has no Recycle Bin.
    Result<void> trash(const NativePath& path) override;
};

}  // namespace rb::os_windows::platform
