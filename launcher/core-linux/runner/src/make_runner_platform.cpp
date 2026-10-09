#include "reboot/os_linux/runner/make_runner_platform.hpp"

#include <utility>

#include "reboot/os_linux/runner/linux_runner_platform.hpp"

namespace rb::os_linux::runner {

std::unique_ptr<ports::IRunnerPlatform> make_runner_platform(const AppLayout& layout,
                                                             ports::IProcessLauncher& processes,
                                                             ports::EnvBlock setup_base) {
    // Under data/, so purge and reset of the launcher data take the runtime with them.
    return std::make_unique<LinuxRunnerPlatform>(processes, std::move(setup_base), layout.root() / "data" / "umu");
}

}  // namespace rb::os_linux::runner
