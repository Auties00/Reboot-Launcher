#pragma once

#include <chrono>
#include <string>

namespace reboot::compat {

// The Steam Linux Runtime build that a runtime setup installed under the launcher's
// UMU_FOLDERS_PATH, as IRunnerPlatform::runtime_setup reports it.
struct SlrInstall {
    std::string build;
    std::chrono::system_clock::time_point installed_at;

    bool operator==(const SlrInstall&) const = default;
};

}  // namespace reboot::compat
