#pragma once

#include <optional>
#include <string>

#include "reboot/foundation/types.hpp"

namespace rb::os_macos::platform {

// sysctlbyname reads; nullopt when the name does not exist on this system.
[[nodiscard]] std::optional<std::string> sysctl_string(const char* name);
[[nodiscard]] std::optional<i64> sysctl_integer(const char* name);

// hw.optional.arm64, which an x86_64 process under Rosetta also sees.
[[nodiscard]] bool apple_silicon();

}  // namespace rb::os_macos::platform
