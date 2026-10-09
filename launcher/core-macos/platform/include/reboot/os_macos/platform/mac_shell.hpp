#pragma once

#include <string_view>

#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/native_path.hpp"
#include "reboot/ports/os_services.hpp"

namespace rb::os_macos::platform {

// Covers no capability ids; IShellLauncher over NSWorkspace and NSFileManager.
class MacShell final : public ports::IShellLauncher {
public:
    // Outside Aqua there is no window server, so every call but trash fails with platform.no_gui_session.
    explicit MacShell(bool aqua_session) noexcept : aqua_session_(aqua_session) {}

    Result<void> open_url(std::string_view https_url) override;
    Result<void> open_path(const NativePath& path) override;
    Result<void> reveal(const NativePath& path) override;
    // Fails rather than deleting permanently on a volume without a trash.
    Result<void> trash(const NativePath& path) override;

private:
    bool aqua_session_ = false;
};

}  // namespace rb::os_macos::platform
