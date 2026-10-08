#include "reboot/integration/prerequisite_spec.hpp"

#include <array>
#include <cstddef>

#include "messages.hpp"

namespace reboot::integration {

const PrerequisiteSpec& prerequisite_spec(PrerequisiteId id) noexcept {
    using enum PrerequisiteImpact;
    // Function-local, so the message ids are initialised before the table reads them.
    static const std::array<PrerequisiteSpec, kPrerequisiteIds.size()> kSpecs{{
        {PrerequisiteId::WindowsMinVersion, BlocksApp, Remedy::None, msg::kGuideWindowsMinVersion},
        {PrerequisiteId::MacAppleSilicon, BlocksPlay, Remedy::None, msg::kGuideMacAppleSilicon},
        {PrerequisiteId::MacMinVersion, BlocksPlay, Remedy::None, msg::kGuideMacMinVersion},
        {PrerequisiteId::MacRosetta, BlocksPlay, Remedy::Install, msg::kGuideMacRosetta},
        {PrerequisiteId::MacAppFirewall, Advisory, Remedy::OpenSettings, msg::kGuideMacAppFirewall},
        {PrerequisiteId::MacLocalNetwork, Advisory, Remedy::OpenSettings, msg::kGuideMacLocalNetwork},
        {PrerequisiteId::LinuxPython3, BlocksPlay, Remedy::None, msg::kGuideLinuxPython3},
        {PrerequisiteId::LinuxVulkan, BlocksPlay, Remedy::None, msg::kGuideLinuxVulkan},
        {PrerequisiteId::LinuxLinger, Advisory, Remedy::Enable, msg::kGuideLinuxLinger},
    }};
    return kSpecs[static_cast<std::size_t>(id)];
}

}  // namespace reboot::integration
