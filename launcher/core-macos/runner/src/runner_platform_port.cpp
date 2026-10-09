#include "reboot/os_macos/runner/make_runner_platform.hpp"
#include "reboot/ports/platform_services.hpp"

namespace reboot::ports {

// The macOS runner keeps nothing under the data root and runs no setup of its own.
std::unique_ptr<IRunnerPlatform> make_runner_platform(const AppLayout&, IProcessLauncher&, EnvBlock) {
    return os_macos::runner::make_runner_platform();
}

}  // namespace reboot::ports
