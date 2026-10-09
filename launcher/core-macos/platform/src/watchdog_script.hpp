#pragma once

#include <optional>
#include <string>

#include "reboot/foundation/types.hpp"

namespace rb::os_macos::platform {

// The watchdog's /bin/sh -c script; without `pid` it kills its own group, which the child leads.
[[nodiscard]] std::string watchdog_script(std::optional<u32> pid);

}  // namespace rb::os_macos::platform
