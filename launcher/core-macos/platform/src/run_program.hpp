#pragma once

#include <chrono>
#include <optional>
#include <string>
#include <vector>

#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/native_path.hpp"

namespace rb::os_macos::platform {

struct ProgramResult {
    std::optional<int> code;
    std::optional<int> signal;
    // stdout, then stderr.
    std::string output;
};

// Runs a system tool in its own process group with stdin at EOF, PATH as its only variable and
// stdout and stderr captured. Blocking: the group is SIGKILLed past `deadline`
// (platform.helper_timeout).
[[nodiscard]] Result<ProgramResult> run_program(const NativePath& program, std::vector<std::string> args,
                                                std::chrono::milliseconds deadline);

}  // namespace rb::os_macos::platform
