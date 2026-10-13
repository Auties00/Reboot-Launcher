#pragma once

#include <memory>

#include "reboot/base/native_path.hpp"

namespace rb::ports {
class IRunnerPlatform;
}

namespace rb::os_macos::runner {

// For PlatformServices::runner. sysctl hw.optional.arm64 reads 1 even for a translated engine.
// `data_root` holds winhost's token files only when confstr names no per-user temp dir.
[[nodiscard]] std::unique_ptr<ports::IRunnerPlatform> make_runner_platform(const NativePath& data_root);

}  // namespace rb::os_macos::runner
