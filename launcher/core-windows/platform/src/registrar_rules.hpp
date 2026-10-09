#pragma once

#include <optional>
#include <span>
#include <string>
#include <string_view>

#include "reboot/foundation/native_path.hpp"
#include "reboot/foundation/types.hpp"
#include "reboot/ports/os_services.hpp"

namespace rb::os_windows::platform {

// The program a registered command runs: the quoted first token, or everything up to the first space.
[[nodiscard]] std::optional<NativePath> command_program(std::string_view command);

// `"<exe>" <args>`, quoting the program as Explorer and the Run key expect.
[[nodiscard]] std::string entry_command(const NativePath& exe, std::string_view args);

// An Explorer StartupApproved value: an odd first byte means the user turned the entry off.
[[nodiscard]] bool startup_disabled(std::span<const u8> approved) noexcept;

// Ours when `program` lies under `install_root`, Stale when it no longer exists, Foreign otherwise.
[[nodiscard]] ports::IntegrationState ownership(const NativePath& program, bool program_exists,
                                                const NativePath& install_root);

}  // namespace rb::os_windows::platform
