#pragma once

#include <memory>

#include "reboot/foundation/paths.hpp"
#include "reboot/ports/process.hpp"

namespace reboot::ports {
class IRunnerPlatform;
}

namespace reboot::os_linux::runner {

// For PlatformServices::runner. `processes` is PlatformServices::processes, which outlives it;
// `setup_base` is EnvBuilder's daemon-base layer.
[[nodiscard]] std::unique_ptr<ports::IRunnerPlatform> make_runner_platform(const AppLayout& layout,
                                                                           ports::IProcessLauncher& processes,
                                                                           ports::EnvBlock setup_base);

}  // namespace reboot::os_linux::runner
