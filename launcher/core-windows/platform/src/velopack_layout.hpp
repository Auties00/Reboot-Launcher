#pragma once

#include <optional>

#include "reboot/foundation/function.hpp"
#include "reboot/foundation/native_path.hpp"

namespace reboot::os_windows::platform {

// <root> when `exe_dir` is <root>\current holding sq.version beside <root>\Update.exe, the layout a
// Velopack per-user install has; nullopt for a portable copy.
[[nodiscard]] std::optional<NativePath> velopack_root_of(const NativePath& exe_dir,
                                                         UniqueFunction<bool(const NativePath&)> file_exists);

}  // namespace reboot::os_windows::platform
