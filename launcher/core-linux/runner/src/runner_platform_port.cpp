#include <utility>

#include "reboot/os_linux/runner/make_runner_platform.hpp"
#include "reboot/ports/platform_services.hpp"

namespace reboot::ports {

std::unique_ptr<IRunnerPlatform> make_runner_platform(const AppLayout& layout, IProcessLauncher& processes,
                                                      EnvBlock setup_base) {
    return os_linux::runner::make_runner_platform(layout, processes, std::move(setup_base));
}

}  // namespace reboot::ports
