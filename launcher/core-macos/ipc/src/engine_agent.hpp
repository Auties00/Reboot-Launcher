#pragma once

#include <string_view>

namespace reboot::os_macos::ipc {

// Copies of os_macos::platform::kEngineAgentLabel and kEngineAgentPlist, which this package may
// not include; all four change together.
inline constexpr std::string_view kEngineAgentLabel = "dev.projectreboot.launcher.engine";
inline constexpr std::string_view kEngineAgentPlist = "dev.projectreboot.launcher.engine.plist";

}  // namespace reboot::os_macos::ipc
