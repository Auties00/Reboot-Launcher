#pragma once

#include <string_view>
#include <vector>

#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/types.hpp"
#include "reboot/ports/os_services.hpp"

namespace reboot::os_windows::platform {

// Covers no capability ids; IPrerequisiteProbe for the Windows floor.
class WindowsPrerequisites final : public ports::IPrerequisiteProbe {
public:
    inline static constexpr std::string_view kMinimumBuildId = "windows.min_build";
    // Windows 10 1809.
    inline static constexpr u32 kMinimumBuild = 17763;

    std::vector<ports::PrerequisiteStatus> check() override;
    // Nothing on Windows can be fixed in place.
    Result<void> remediate(std::string_view id) override;
};

}  // namespace reboot::os_windows::platform
