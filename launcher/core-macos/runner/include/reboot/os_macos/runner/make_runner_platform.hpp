#pragma once

#include <memory>

namespace reboot::ports {
class IRunnerPlatform;
}

namespace reboot::os_macos::runner {

// For PlatformServices::runner. sysctl hw.optional.arm64 reads 1 even for a translated engine.
[[nodiscard]] std::unique_ptr<ports::IRunnerPlatform> make_runner_platform();

}  // namespace reboot::os_macos::runner
