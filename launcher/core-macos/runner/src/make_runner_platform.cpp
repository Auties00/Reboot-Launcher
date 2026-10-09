#include "reboot/os_macos/runner/make_runner_platform.hpp"

#include <sys/types.h>
#include <sys/sysctl.h>

#include <cstddef>

#include "reboot/os_macos/runner/dxmt_wine_runner_platform.hpp"

namespace rb::os_macos::runner {

std::unique_ptr<ports::IRunnerPlatform> make_runner_platform() {
    int arm64 = 0;
    std::size_t size = sizeof arm64;
    // A failed sysctl counts as Intel.
    const bool apple_silicon = ::sysctlbyname("hw.optional.arm64", &arm64, &size, nullptr, 0) == 0 && arm64 == 1;
    return std::make_unique<DxmtWineRunnerPlatform>(apple_silicon ? HostCpu::AppleSilicon : HostCpu::Intel);
}

}  // namespace rb::os_macos::runner
