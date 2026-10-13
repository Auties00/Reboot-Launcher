#pragma once

#include <optional>
#include <string>
#include <string_view>

#include "reboot/components/manifest_platform.hpp"
#include "reboot/foundation/native_path.hpp"
#include "reboot/integration/integration_targets.hpp"

namespace rb::engine {

inline constexpr components::ManifestOs kHostOs = components::build_platform().os;
inline constexpr bool kWindowsHost = kHostOs == components::ManifestOs::Windows;
inline constexpr bool kLinuxHost = kHostOs == components::ManifestOs::Linux;
// This program's file name in IPlatformPaths::exe_dir.
inline constexpr std::string_view kEngineExe = kWindowsHost ? "reboot-engine.exe" : "reboot-engine";

// What OS integration entries point at, for this OS's entry flavor.
[[nodiscard]] integration::IntegrationTargets integration_targets(NativePath engine_exe);

// A variable of the engine's own environment in UTF-8; nullopt when unset or empty.
[[nodiscard]] std::optional<std::string> own_environment(const char* name);

}  // namespace rb::engine
