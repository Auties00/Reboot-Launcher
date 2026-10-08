#pragma once

#include <filesystem>
#include <string>
#include <string_view>

#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/native_path.hpp"

namespace reboot::os_linux::runner {

// Blocking. A missing entry is a not_found status, not an error.
[[nodiscard]] Result<std::filesystem::file_status> status_of(const NativePath& path);

[[nodiscard]] bool is_executable_file(const std::filesystem::file_status& status);

// Blocking. `runtime_dir`, or its only subdirectory when the archive kept a top-level folder and
// `marker` is not directly inside.
[[nodiscard]] Result<NativePath> archive_root(const NativePath& runtime_dir, std::string_view marker);

// Blocking.
[[nodiscard]] Result<std::string> read_text(const NativePath& path);

}  // namespace reboot::os_linux::runner
