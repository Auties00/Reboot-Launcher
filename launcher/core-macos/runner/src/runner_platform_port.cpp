#include "reboot/os_macos/runner/make_runner_platform.hpp"
#include "reboot/ports/platform_services.hpp"

namespace rb::ports {

// The macOS runner runs no setup of its own; the data root is only its token files' fallback.
std::unique_ptr<IRunnerPlatform> make_runner_platform(const AppLayout& layout, IProcessLauncher&, EnvBlock) {
    return os_macos::runner::make_runner_platform(layout.root() / "data");
}

}  // namespace rb::ports
