#pragma once

#include <optional>

#include "reboot/foundation/native_path.hpp"

namespace reboot::os_macos::platform {

// <X>.app when `exe_dir` is <X>.app/Contents/MacOS; nullopt for a dev build.
[[nodiscard]] std::optional<NativePath> app_bundle_of(const NativePath& exe_dir);

}  // namespace reboot::os_macos::platform
