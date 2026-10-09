#pragma once

#include <optional>
#include <string>

#include "reboot/components/manifest_platform.hpp"

namespace rb::engine {

inline constexpr components::ManifestOs kHostOs = components::build_platform().os;
inline constexpr bool kWindowsHost = kHostOs == components::ManifestOs::Windows;
inline constexpr bool kLinuxHost = kHostOs == components::ManifestOs::Linux;

// A variable of the engine's own environment in UTF-8; nullopt when unset or empty.
[[nodiscard]] std::optional<std::string> own_environment(const char* name);

}  // namespace rb::engine
