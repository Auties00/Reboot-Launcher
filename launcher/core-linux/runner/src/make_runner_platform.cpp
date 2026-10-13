#include "reboot/os_linux/runner/make_runner_platform.hpp"

#include <cstdlib>
#include <optional>
#include <string_view>
#include <utility>

#include "reboot/os_linux/runner/linux_runner_platform.hpp"

namespace rb::os_linux::runner {

std::unique_ptr<ports::IRunnerPlatform> make_runner_platform(const AppLayout& layout,
                                                             ports::IProcessLauncher& processes,
                                                             ports::EnvBlock setup_base) {
    // Under data/, so purge and reset of the launcher data take the runtime with them.
    const NativePath data = layout.root() / "data";
    const char* const runtime = std::getenv("XDG_RUNTIME_DIR");
    return std::make_unique<LinuxRunnerPlatform>(
        processes, std::move(setup_base), data / "umu",
        LinuxRunnerPlatform::token_base(runtime ? std::optional<std::string_view>{runtime} : std::nullopt, data));
}

}  // namespace rb::os_linux::runner
