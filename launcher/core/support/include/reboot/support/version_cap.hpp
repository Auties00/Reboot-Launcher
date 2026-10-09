#pragma once

#include <optional>

#include "reboot/foundation/version.hpp"

namespace rb::support {

inline constexpr GameVersion kMaxSupportedVersion{30, 10, std::nullopt};

// game-builds.+3: compares major.minor only, so every 30.10.x build is at the cap, and a
// changelist, which GameVersion never holds, cannot move a build under it.
[[nodiscard]] constexpr bool above_version_cap(const GameVersion& version) noexcept {
    if (version.major != kMaxSupportedVersion.major) return version.major > kMaxSupportedVersion.major;
    return version.minor > kMaxSupportedVersion.minor;
}

}  // namespace rb::support
