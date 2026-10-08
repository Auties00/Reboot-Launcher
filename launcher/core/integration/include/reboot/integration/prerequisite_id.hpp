#pragma once

#include <array>
#include <cstddef>
#include <optional>
#include <string_view>

#include "reboot/foundation/types.hpp"

namespace reboot::integration {

// The firewall, Local Network and linger ids concern hosting; the others concern the app or play.
enum class PrerequisiteId : u8 {
    WindowsMinVersion,
    MacAppleSilicon,
    MacMinVersion,
    MacRosetta,
    MacAppFirewall,
    MacLocalNetwork,
    LinuxPython3,
    LinuxVulkan,
    LinuxLinger,
};

// The stable ids IPrerequisiteProbe reports and the API carries.
inline constexpr std::array<std::string_view, 9> kPrerequisiteIds{
    "win.os_min_1809",        "mac.apple_silicon",     "mac.os_min_14", "mac.rosetta", "mac.app_firewall_state",
    "mac.local_network_hint", "linux.python3",         "linux.vulkan",  "linux.linger"};

[[nodiscard]] constexpr std::string_view to_string(PrerequisiteId id) noexcept {
    return kPrerequisiteIds[static_cast<std::size_t>(id)];
}

[[nodiscard]] constexpr std::optional<PrerequisiteId> parse_prerequisite_id(std::string_view text) noexcept {
    for (std::size_t i = 0; i < kPrerequisiteIds.size(); ++i)
        if (kPrerequisiteIds[i] == text) return static_cast<PrerequisiteId>(i);
    return std::nullopt;
}

}  // namespace reboot::integration
