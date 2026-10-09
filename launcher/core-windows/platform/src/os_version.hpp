#pragma once

#include <string>

#include "reboot/foundation/types.hpp"

namespace reboot::os_windows::platform {

struct OsVersion {
    u32 major = 0;
    u32 minor = 0;
    u32 build = 0;
};

// RtlGetVersion, since GetVersionEx reports the manifest's compatibility version instead.
[[nodiscard]] OsVersion read_os_version() noexcept;

// "x86_64", "arm64" or "x86" for the native machine, which IsWow64Process2 reports even to an
// emulated x64 process on ARM64.
[[nodiscard]] std::string native_arch();

}  // namespace reboot::os_windows::platform
